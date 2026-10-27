#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char BYTE;

typedef enum DlumaWorkshopPixelLayout {
    DLUMA_WORKSHOP_PIXEL_LAYOUT_UNKNOWN = 0,
    DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE,
    DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD
} DlumaWorkshopPixelLayout;

static void bgra_to_rgba_in_place(BYTE *pixels, size_t pixel_count)
{
    size_t i;
    for (i = 0; i < pixel_count; ++i) {
        BYTE *pixel = pixels + i * 4U;
        BYTE blue = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = blue;
    }
}

typedef struct FakePixelJob {
    BYTE pixels[16];
    DlumaWorkshopPixelLayout layout;
    unsigned int conversions;
} FakePixelJob;

static void fake_worker_prepare(FakePixelJob *job)
{
    assert(job->layout == DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE);
    bgra_to_rgba_in_place(job->pixels, 4U);
    job->layout = DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD;
    ++job->conversions;
}

static void fake_rebind(FakePixelJob *job)
{
    /* Row bindings consume a ready immutable job; they never transform it. */
    assert(job->layout == DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD);
}

int main(void)
{
    static const BYTE input_bgra[16] = {
        0x00, 0x00, 0xff, 0xff, /* red */
        0xff, 0x00, 0x00, 0xff, /* blue */
        0x00, 0xff, 0x00, 0xff, /* green */
        0x11, 0x22, 0x33, 0x44  /* alpha test */
    };
    static const BYTE expected_rgba[16] = {
        0xff, 0x00, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff,
        0x00, 0xff, 0x00, 0xff,
        0x33, 0x22, 0x11, 0x44
    };
    FakePixelJob job;
    BYTE preserved[sizeof(job.pixels)];
    const unsigned int width = 2U;
    const unsigned int height = 2U;
    const unsigned int row_pitch = width * 4U;

    memset(&job, 0, sizeof(job));
    memcpy(job.pixels, input_bgra, sizeof(input_bgra));
    job.layout = DLUMA_WORKSHOP_PIXEL_LAYOUT_BGRA8_CACHE;
    assert(sizeof(job.pixels) == width * height * 4U);
    assert(row_pitch == width * 4U);
    fake_worker_prepare(&job);
    assert(job.layout == DLUMA_WORKSHOP_PIXEL_LAYOUT_RGBA8_DYNAMIC_UPLOAD);
    assert(job.conversions == 1U);
    assert(memcmp(job.pixels, expected_rgba, sizeof(expected_rgba)) == 0);
    /* Green and alpha remain intact while red/blue exchange places. */
    assert(job.pixels[8] == 0x00 && job.pixels[9] == 0xff &&
           job.pixels[10] == 0x00 && job.pixels[11] == 0xff);
    assert(job.pixels[15] == 0x44);
    memcpy(preserved, job.pixels, sizeof(preserved));
    fake_rebind(&job);
    fake_rebind(&job);
    assert(job.conversions == 1U);
    assert(memcmp(job.pixels, preserved, sizeof(preserved)) == 0);
    puts("workshop_preview_color_fake_harness: 14/14 ok");
    return 0;
}
