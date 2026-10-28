#define _CRT_SECURE_NO_WARNINGS
#include "dluma_api.h"
#include "controller.h"
#include "engine_adapter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

using thirdperson::Controller;
using thirdperson::ControllerConfig;
using thirdperson::EngineAdapter;
using thirdperson::KeyBindings;
using thirdperson::Offsets;

constexpr uint64_t UpdateCallback = 1;
constexpr uint64_t ConfigurationCallback = 2;
constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;

DlumaStringView view(const char* text)
{
    return {text, static_cast<uint64_t>(std::strlen(text))};
}

template<class T> T record()
{
    T value{};
    value.struct_size = static_cast<uint32_t>(sizeof(value));
    value.struct_version = 1;
    return value;
}

template<class T> T resultRecord()
{
    T value = record<T>();
    value.call = record<DlumaResult>();
    return value;
}

struct Runtime {
    const DlumaApiV1* api = nullptr;
    DlumaCoreV1 core{};
    DlumaEventsV1 events{};
    DlumaConfigurationV1 configuration{};
    DlumaStorageV1 storage{};
    Controller controller{};
    EngineAdapter adapter{};
    uint64_t updateToken = 0;
    uint64_t configurationToken = 0;
    uint32_t mapGeneration = 0;
    bool mapGenerationKnown = false;
    uint64_t nextSaveAttempt = 0;
    bool started = false;
    bool scopeActive = false;
    bool savePending = false;
    bool saveWarning = false;

    template<class T> bool service(const char* id, T& table)
    {
        table = {};
        if (!api || !api->get_service)
            return false;
        if (api->get_service(api->context, view(id), 1, &table,
                static_cast<uint32_t>(sizeof(table))) != DLUMA_STATUS_OK)
            return false;
        return table.struct_size >= sizeof(table) && table.version == 1;
    }

    void log(const char* level, const char* message) const
    {
        if (!core.log)
            return;
        auto request = record<DlumaLogRequest>();
        auto result = resultRecord<DlumaCallResult>();
        request.level = view(level);
        request.message = view(message);
        core.log(core.context, &request, &result);
    }

    uint64_t now() const
    {
        if (!core.monotonic_ms)
            return 0;
        auto request = record<DlumaNoRequest>();
        auto result = resultRecord<DlumaU64Result>();
        return core.monotonic_ms(core.context, &request, &result) == DLUMA_STATUS_OK
            ? result.value : 0;
    }

    int preference(const char* key, int fallback, int minimum, int maximum) const
    {
        if (!configuration.get)
            return fallback;
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view(key);
        if (configuration.get(configuration.context, &request, &result) != DLUMA_STATUS_OK ||
            result.value.kind != DLUMA_SCALAR_INTEGER ||
            result.value.integer_value < minimum || result.value.integer_value > maximum)
            return fallback;
        return static_cast<int>(result.value.integer_value);
    }

    bool booleanPreference(const char* key, bool fallback) const
    {
        if (!configuration.get)
            return fallback;
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view(key);
        if (configuration.get(configuration.context, &request, &result) != DLUMA_STATUS_OK ||
            result.value.kind != DLUMA_SCALAR_BOOLEAN)
            return fallback;
        return result.value.integer_value != 0;
    }

    bool firstPersonAimPreference() const
    {
        if (!configuration.get) return false;
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view("aim_view");
        if (configuration.get(configuration.context, &request, &result) != DLUMA_STATUS_OK ||
            result.value.kind != DLUMA_SCALAR_STRING || !result.value.string_value.data) return false;
        constexpr char value[] = "first_person";
        return result.value.string_value.size == sizeof(value) - 1 &&
            std::memcmp(result.value.string_value.data, value, sizeof(value) - 1) == 0;
    }

    bool freeCameraPreference() const
    {
        if (!configuration.get) return false;
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view("camera_style");
        if (configuration.get(configuration.context, &request, &result) != DLUMA_STATUS_OK ||
            result.value.kind != DLUMA_SCALAR_STRING || !result.value.string_value.data) return false;
        constexpr char value[] = "free_camera";
        return result.value.string_value.size == sizeof(value) - 1 &&
            std::memcmp(result.value.string_value.data, value, sizeof(value) - 1) == 0;
    }

    void loadConfiguration()
    {
        KeyBindings keys{};
        keys.toggle = preference("toggle_key", keys.toggle, 1, 254);
        keys.orbit = preference("orbit_key", keys.orbit, 1, 254);
        keys.left = preference("left_key", keys.left, 1, 254);
        keys.right = preference("right_key", keys.right, 1, 254);
        keys.up = preference("up_key", keys.up, 1, 254);
        keys.down = preference("down_key", keys.down, 1, 254);
        keys.closer = preference("closer_key", keys.closer, 1, 254);
        keys.farther = preference("farther_key", keys.farther, 1, 254);
        keys.reset = preference("reset_key", keys.reset, 1, 254);
        adapter.setBindings(keys);
        ControllerConfig config{};
        config.adjustmentSpeed = 0.75f *
            static_cast<float>(preference("speed_percent", 100, 10, 500)) / 100.0f;
        config.returnSeconds =
            static_cast<float>(preference("return_time_ms", 250, 10, 2000)) / 1000.0f;
        config.smoothCamera = booleanPreference("smooth_camera", true);
        config.smoothingSeconds =
            static_cast<float>(preference("smoothing_time_ms", 120, 20, 300)) / 1000.0f;
        config.horizontalAngle = static_cast<float>(
            preference("horizontal_angle_deg", 0, -180, 180)) * DegreesToRadians;
        config.verticalAngle = static_cast<float>(
            preference("vertical_angle_deg", 0, -85, 85)) * DegreesToRadians;
        config.aimFirstPerson = firstPersonAimPreference();
        config.freeCamera = freeCameraPreference();
        config.firstPersonActions = booleanPreference("first_person_actions", false);
        config.aimDistanceReduction = static_cast<float>(preference("aim_distance_percent", 20, 0, 60)) / 100.0f;
        config.aimMagnification = static_cast<float>(preference("aim_magnification_percent", 125, 100, 200)) / 100.0f;
        controller.configure(config);
    }

    float storedNumber(const char* key, float fallback, bool* found = nullptr) const
    {
        if (!storage.get)
            return fallback;
        auto request = record<DlumaStorageGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view(key);
        if (storage.get(storage.context, &request, &result) != DLUMA_STATUS_OK)
            return fallback;
        double value = fallback;
        if (result.value.kind == DLUMA_SCALAR_NUMBER)
            value = result.value.number_value;
        else if (result.value.kind == DLUMA_SCALAR_INTEGER)
            value = static_cast<double>(result.value.integer_value);
        else
            return fallback;
        if (!std::isfinite(value) ||
            value < -static_cast<double>((std::numeric_limits<float>::max)()) ||
            value > static_cast<double>((std::numeric_limits<float>::max)()))
            return fallback;
        if (found) *found = true;
        return static_cast<float>(value);
    }

    void loadOffsets()
    {
        Offsets offsets{};
        auto request = record<DlumaStorageGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view("camera_offsets_v2");
        if (storage.get && storage.get(storage.context, &request, &result) == DLUMA_STATUS_OK &&
            result.value.kind == DLUMA_SCALAR_STRING && result.value.string_value.data &&
            result.value.string_value.size > 0 && result.value.string_value.size < 128) {
            char text[128]{};
            std::memcpy(text, result.value.string_value.data, static_cast<size_t>(result.value.string_value.size));
            int consumed = 0;
            if (std::sscanf(text, "v2 %f %f %f %n", &offsets.lateral, &offsets.height, &offsets.distance, &consumed) == 3 &&
                consumed > 0 && text[consumed] == '\0' && controller.setOffsets(offsets)) return;
        }
        offsets = {};
        bool legacy = false;
        bool legacyLateral = false;
        offsets.lateral = storedNumber("lateral_offset", offsets.lateral, &legacyLateral);
        if (legacyLateral) offsets.lateral = -offsets.lateral;
        legacy = legacyLateral;
        offsets.height = storedNumber("height_offset", offsets.height, &legacy);
        offsets.distance = storedNumber("camera_distance", offsets.distance, &legacy);
        controller.setOffsets(offsets);
        savePending = legacy;
    }

    bool storeOffsets(const Offsets& offsets) const
    {
        if (!storage.set)
            return false;
        char text[128]{};
        const int length = std::snprintf(text, sizeof(text), "v2 %.9g %.9g %.9g",
            static_cast<double>(offsets.lateral), static_cast<double>(offsets.height), static_cast<double>(offsets.distance));
        if (length < 0 || static_cast<size_t>(length) >= sizeof(text)) return false;
        auto request = record<DlumaStorageSetRequest>();
        auto result = resultRecord<DlumaBoolResult>();
        request.key = view("camera_offsets_v2");
        request.value.kind = DLUMA_SCALAR_STRING;
        request.value.string_value = view(text);
        return storage.set(storage.context, &request, &result) == DLUMA_STATUS_OK && result.value;
    }

    void saveOffsets(bool flush)
    {
        savePending = controller.takePendingSave(flush) || savePending;
        if (!savePending)
            return;
        const uint64_t timestamp = now();
        if (!flush && timestamp && timestamp < nextSaveAttempt)
            return;
        const Offsets offsets = controller.offsets();
        savePending = !storeOffsets(offsets);
        nextSaveAttempt = timestamp + 1000;
        if (savePending && !saveWarning) {
            log("WARN", "Third Person could not save its camera offsets; it will retry.");
            saveWarning = true;
        } else if (!savePending) {
            saveWarning = false;
        }
    }

    void cancel(uint64_t& token)
    {
        if (token && core.token_cancel) {
            auto request = record<DlumaTokenRequest>();
            auto result = resultRecord<DlumaBoolResult>();
            request.token = token;
            core.token_cancel(core.context, &request, &result);
        }
        token = 0;
    }

    DlumaStatus prepare(const DlumaApiV1* host)
    {
        if (!host || host->struct_size < sizeof(*host) ||
            host->abi_version != DLUMA_API_ABI_VERSION || host->api_major != 1 ||
            !host->get_service)
            return DLUMA_STATUS_INVALID_ARGUMENT;
        api = host;
        if (!service(DLUMA_SERVICE_CORE_V1, core) || !core.log || !core.token_cancel)
            return DLUMA_STATUS_UNAVAILABLE;
        return DLUMA_STATUS_OK;
    }

    DlumaStatus start();

    void rollbackStart()
    {
        scopeActive = false;
        cancel(updateToken);
        cancel(configurationToken);
        adapter.shutdown();
        started = false;
    }

    void stop()
    {
        scopeActive = false;
        cancel(updateToken);
        cancel(configurationToken);
        adapter.shutdown();
        saveOffsets(true);
        controller.resetSession();
        started = false;
        api = nullptr;
        events = {};
        configuration = {};
        storage = {};
        core = {};
    }
};

Runtime runtime;

void engineLog(const char* level, const char* message)
{
    runtime.log(level, message);
}

DlumaStatus Runtime::start()
{
    if (started)
        return DLUMA_STATUS_OK;
    if (!service(DLUMA_SERVICE_EVENTS_V1, events) || !events.subscribe_update)
        return DLUMA_STATUS_UNAVAILABLE;
    if (!service(DLUMA_SERVICE_CONFIGURATION_V1, configuration) || !configuration.get)
        return DLUMA_STATUS_UNAVAILABLE;
    if (!service(DLUMA_SERVICE_STORAGE_V1, storage) || !storage.get || !storage.set)
        return DLUMA_STATUS_UNAVAILABLE;

    loadOffsets();
    KeyBindings keys{};
    const bool supported = adapter.initialize(&controller, keys, engineLog);
    loadConfiguration();
    if (!supported) {
        started = true;
        return DLUMA_STATUS_OK;
    }

    auto request = record<DlumaEventSubscribeRequest>();
    auto result = resultRecord<DlumaTokenResult>();
    request.callback_id = UpdateCallback;
    const DlumaStatus updateStatus = events.subscribe_update(events.context, &request, &result);
    updateToken = result.token;
    if (updateStatus != DLUMA_STATUS_OK || !updateToken) {
        rollbackStart();
        return DLUMA_STATUS_UNAVAILABLE;
    }
    if (configuration.subscribe_changed) {
        request.callback_id = ConfigurationCallback;
        result = resultRecord<DlumaTokenResult>();
        if (configuration.subscribe_changed(configuration.context, &request, &result) == DLUMA_STATUS_OK)
            configurationToken = result.token;
    }
    started = true;
    return DLUMA_STATUS_OK;
}

DlumaStatus DLUMA_API_CALL preGame(const DlumaApiV1* api)
{
    return runtime.prepare(api);
}

DlumaStatus DLUMA_API_CALL postGame(const DlumaApiV1*)
{
    try {
        return runtime.start();
    } catch (...) {
        runtime.rollbackStart();
        runtime.log("ERROR", "Third Person could not initialize.");
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
}

DlumaStatus DLUMA_API_CALL scopeActivated(const DlumaApiV1*, const DlumaScopeActivationV1* activation)
{
    if (!activation || activation->struct_size < sizeof(*activation))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    runtime.scopeActive = true;
    runtime.adapter.activate();
    return DLUMA_STATUS_OK;
}

void DLUMA_API_CALL scopeDeactivated(const DlumaApiV1*, const DlumaScopeActivationV1*)
{
    runtime.scopeActive = false;
    runtime.adapter.deactivate();
    runtime.saveOffsets(true);
}

void DLUMA_API_CALL shutdown(const DlumaApiV1*, uint32_t)
{
    runtime.stop();
}

DlumaStatus DLUMA_API_CALL event(const DlumaApiV1*, const DlumaModEventV1* value)
{
    if (!value || value->struct_size < sizeof(*value))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!runtime.started)
        return DLUMA_STATUS_OK;
    if (value->kind == DLUMA_MOD_EVENT_CONFIGURATION_CHANGED &&
        value->callback_id == ConfigurationCallback) {
        runtime.loadConfiguration();
    } else if (runtime.scopeActive && value->kind == DLUMA_MOD_EVENT_UPDATE &&
        value->callback_id == UpdateCallback) {
        if (!runtime.mapGenerationKnown || runtime.mapGeneration != value->map_generation) {
            runtime.mapGeneration = value->map_generation;
            runtime.mapGenerationKnown = true;
            runtime.adapter.activate();
        }
        runtime.adapter.tick();
        runtime.saveOffsets(false);
    }
    return DLUMA_STATUS_OK;
}

const DlumaNativeModApiV2 callbacks = {
    sizeof(DlumaNativeModApiV2), DLUMA_NATIVE_MOD_ABI_V2,
    preGame, postGame, scopeActivated, scopeDeactivated, shutdown, event, nullptr
};

} // namespace

extern "C" DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL DLUMANativeMod_GetApiV2()
{
    return &callbacks;
}
