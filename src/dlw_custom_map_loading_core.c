#include "dlw_custom_map_loading_core.h"
#include "dlw_common.h"

#define DLUMA_LOADING_DEF_MAX_DIMENSION 16384U
#define DLUMA_LOADING_DEF_MAX_TOKEN 512U

typedef enum DlumaLoadingTokenKind
{
    DLUMA_LOADING_TOKEN_END = 0,
    DLUMA_LOADING_TOKEN_IDENTIFIER,
    DLUMA_LOADING_TOKEN_STRING,
    DLUMA_LOADING_TOKEN_NUMBER,
    DLUMA_LOADING_TOKEN_PUNCTUATION,
    DLUMA_LOADING_TOKEN_INVALID
} DlumaLoadingTokenKind;

typedef struct DlumaLoadingToken
{
    unsigned int kind;
    unsigned int number;
    char punctuation;
    char text[DLUMA_LOADING_DEF_MAX_TOKEN];
} DlumaLoadingToken;

typedef struct DlumaLoadingLexer
{
    const char *text;
    unsigned int size;
    unsigned int cursor;
} DlumaLoadingLexer;

static char dluma_loading_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int dluma_loading_text_equal(const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right)
        return 0;
    while (left[index] && right[index])
    {
        if (dluma_loading_lower(left[index]) !=
            dluma_loading_lower(right[index]))
            return 0;
        ++index;
    }
    return left[index] == 0 && right[index] == 0;
}

static int dluma_loading_text_starts_with(
    const char *text, unsigned int available, const char *prefix)
{
    unsigned int index = 0;
    if (!text || !prefix)
        return 0;
    while (prefix[index])
    {
        if (index >= available ||
            dluma_loading_lower(text[index]) !=
                dluma_loading_lower(prefix[index]))
            return 0;
        ++index;
    }
    return 1;
}

unsigned int dluma_custom_map_loading_directive_mask(const char *name)
{
    if (dluma_loading_text_equal(name, "levelname") ||
        dluma_loading_text_equal(name, "levelnamelocalizationid"))
        return DLUMA_LOADING_DIRECTIVE_TITLE;
    if (dluma_loading_text_equal(name, "loadingforceobjectivetext"))
        return DLUMA_LOADING_DIRECTIVE_OBJECTIVE;
    if (dluma_loading_text_equal(name, "loadingforceobjectivedesctext"))
        return DLUMA_LOADING_DIRECTIVE_DESCRIPTION;
    if (dluma_loading_text_equal(name, "loadingbackground") ||
        dluma_loading_text_equal(name, "loadingmaterial"))
        return DLUMA_LOADING_DIRECTIVE_VISUAL;
    return 0;
}

unsigned int dluma_custom_map_loading_generated_mask(
    int workshop_presentation_enabled,
    unsigned int authored_directives,
    int preview_ready)
{
    unsigned int generated = 0;
    if (!workshop_presentation_enabled)
        return 0;
    if (!(authored_directives & DLUMA_LOADING_DIRECTIVE_TITLE))
        generated |= DLUMA_LOADING_DIRECTIVE_TITLE;
    if (!(authored_directives & DLUMA_LOADING_DIRECTIVE_OBJECTIVE))
        generated |= DLUMA_LOADING_DIRECTIVE_OBJECTIVE;
    if (!(authored_directives & DLUMA_LOADING_DIRECTIVE_DESCRIPTION))
        generated |= DLUMA_LOADING_DIRECTIVE_DESCRIPTION;
    if (preview_ready &&
        !(authored_directives & DLUMA_LOADING_DIRECTIVE_VISUAL))
        generated |= DLUMA_LOADING_DIRECTIVE_VISUAL;
    return generated;
}

int dluma_custom_map_loading_description_only_needed(
    int workshop_presentation_enabled,
    int content_fix_enabled,
    unsigned int authored_directives,
    int generated_description,
    int objective_has_text,
    int description_has_text)
{
    if (objective_has_text || !description_has_text)
        return 0;
    if (workshop_presentation_enabled && generated_description)
        return 1;
    return content_fix_enabled &&
        (authored_directives & DLUMA_LOADING_DIRECTIVE_DESCRIPTION);
}

static int dluma_loading_utf8_sequence(
    const unsigned char *text, unsigned int available,
    unsigned int *length_out, unsigned int *codepoint_out)
{
    unsigned int length;
    unsigned int codepoint;
    unsigned char first;
    unsigned int index;
    if (!text || !available || !length_out)
        return 0;
    first = text[0];
    if (first < 0x80U)
    {
        *length_out = 1U;
        if (codepoint_out)
            *codepoint_out = first;
        return first != 0U;
    }
    if (first >= 0xC2U && first <= 0xDFU)
    {
        length = 2U;
        codepoint = first & 0x1FU;
    }
    else if (first >= 0xE0U && first <= 0xEFU)
    {
        length = 3U;
        codepoint = first & 0x0FU;
    }
    else if (first >= 0xF0U && first <= 0xF4U)
    {
        length = 4U;
        codepoint = first & 0x07U;
    }
    else
        return 0;
    if (length > available)
        return 0;
    for (index = 1U; index < length; ++index)
    {
        if ((text[index] & 0xC0U) != 0x80U)
            return 0;
        codepoint = (codepoint << 6U) | (text[index] & 0x3FU);
    }
    if ((length == 3U && codepoint < 0x800U) ||
        (length == 4U && codepoint < 0x10000U) ||
        codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        return 0;
    *length_out = length;
    if (codepoint_out)
        *codepoint_out = codepoint;
    return 1;
}

static int dluma_loading_append_byte(
    char *output, unsigned int output_capacity,
    unsigned int *write, char value)
{
    if (!output || !output_capacity || !write ||
        *write + 1U >= output_capacity)
        return 0;
    output[(*write)++] = value;
    return 1;
}

static int dluma_loading_append_newline(
    char *output, unsigned int output_capacity,
    unsigned int *write)
{
    if (*write && output[*write - 1U] == '\n')
        return 1;
    return dluma_loading_append_byte(
        output, output_capacity, write, '\n');
}

static int dluma_loading_markup_end(
    const char *input, unsigned int input_size,
    unsigned int start, char close, unsigned int *end_out)
{
    unsigned int cursor;
    if (!input || start >= input_size || !end_out)
        return 0;
    cursor = start + 1U;
    while (cursor < input_size && cursor - start <= 160U)
    {
        unsigned char value = (unsigned char)input[cursor];
        if (value == (unsigned char)close)
        {
            *end_out = cursor;
            return 1;
        }
        if (value == '\r' || value == '\n' || value == 0U)
            return 0;
        ++cursor;
    }
    return 0;
}

typedef enum DlumaLoadingMarkupKind
{
    DLUMA_LOADING_MARKUP_NONE = 0,
    DLUMA_LOADING_MARKUP_INLINE,
    DLUMA_LOADING_MARKUP_BLOCK,
    DLUMA_LOADING_MARKUP_BREAK,
    DLUMA_LOADING_MARKUP_LIST_ITEM
} DlumaLoadingMarkupKind;

static int dluma_loading_markup_name_equal(
    const char *name, unsigned int length, const char *expected)
{
    unsigned int index = 0;
    if (!name || !expected)
        return 0;
    while (index < length && expected[index])
    {
        if (dluma_loading_lower(name[index]) !=
            dluma_loading_lower(expected[index]))
            return 0;
        ++index;
    }
    return index == length && !expected[index];
}

static DlumaLoadingMarkupKind dluma_loading_markup_kind(
    const char *input, unsigned int start, unsigned int end,
    int *closing_out)
{
    static const char *inline_tags[] = {
        "b", "i", "u", "s", "strike", "spoiler", "url",
        "a", "strong", "em", "img", "previewyoutube"};
    static const char *block_tags[] = {
        "h1", "h2", "h3", "p", "quote", "code", "list",
        "olist", "ul", "ol", "table", "tr", "th", "td"};
    const char *tag;
    unsigned int length;
    unsigned int name_length = 0;
    unsigned int index;
    int closing = 0;
    if (closing_out)
        *closing_out = 0;
    if (!input || end <= start + 1U)
        return DLUMA_LOADING_MARKUP_NONE;
    tag = input + start + 1U;
    length = end - start - 1U;
    while (length && (*tag == ' ' || *tag == '\t'))
    {
        ++tag;
        --length;
    }
    if (length && *tag == '/')
    {
        closing = 1;
        ++tag;
        --length;
        while (length && (*tag == ' ' || *tag == '\t'))
        {
            ++tag;
            --length;
        }
    }
    while (name_length < length &&
           tag[name_length] != ' ' &&
           tag[name_length] != '\t' &&
           tag[name_length] != '=' &&
           tag[name_length] != '/')
        ++name_length;
    if (!name_length)
        return DLUMA_LOADING_MARKUP_NONE;
    if (closing_out)
        *closing_out = closing;
    if (name_length == 1U && tag[0] == '*')
        return DLUMA_LOADING_MARKUP_LIST_ITEM;
    if (dluma_loading_markup_name_equal(tag, name_length, "li"))
        return DLUMA_LOADING_MARKUP_LIST_ITEM;
    if (dluma_loading_markup_name_equal(tag, name_length, "br") ||
        dluma_loading_markup_name_equal(tag, name_length, "hr"))
        return DLUMA_LOADING_MARKUP_BREAK;
    for (index = 0; index < DLUMA_ARRAY_COUNT(block_tags); ++index)
        if (dluma_loading_markup_name_equal(
                tag, name_length, block_tags[index]))
            return DLUMA_LOADING_MARKUP_BLOCK;
    for (index = 0; index < DLUMA_ARRAY_COUNT(inline_tags); ++index)
        if (dluma_loading_markup_name_equal(
                tag, name_length, inline_tags[index]))
            return DLUMA_LOADING_MARKUP_INLINE;
    return DLUMA_LOADING_MARKUP_NONE;
}

static int dluma_loading_codepoint_is_control(unsigned int codepoint)
{
    return (codepoint >= 0x80U && codepoint <= 0x9FU) ||
        (codepoint >= 0x200BU && codepoint <= 0x200FU) ||
        (codepoint >= 0x202AU && codepoint <= 0x202EU) ||
        (codepoint >= 0x2060U && codepoint <= 0x206FU) ||
        codepoint == 0xFEFFU;
}

static unsigned int dluma_loading_entity(
    const char *input, unsigned int available, char *value_out)
{
    if (available >= 5U &&
        dluma_loading_text_starts_with(input, available, "&amp;"))
    {
        *value_out = '&';
        return 5U;
    }
    if (available >= 4U &&
        dluma_loading_text_starts_with(input, available, "&lt;"))
    {
        *value_out = '<';
        return 4U;
    }
    if (available >= 4U &&
        dluma_loading_text_starts_with(input, available, "&gt;"))
    {
        *value_out = '>';
        return 4U;
    }
    if (available >= 6U &&
        dluma_loading_text_starts_with(input, available, "&quot;"))
    {
        *value_out = '"';
        return 6U;
    }
    if (available >= 5U &&
        dluma_loading_text_starts_with(input, available, "&#39;"))
    {
        *value_out = '\'';
        return 5U;
    }
    return 0U;
}

int dluma_custom_map_loading_plain_text(
    const char *input, unsigned int input_size,
    char *output, unsigned int output_capacity)
{
    unsigned int read = 0;
    unsigned int write = 0;
    if (!output || !output_capacity)
        return 0;
    output[0] = 0;
    if (!input && input_size)
        goto invalid;
    while (read < input_size)
    {
        unsigned char value = (unsigned char)input[read];
        unsigned int sequence = 0;
        unsigned int codepoint = 0;
        unsigned int markup_end = 0;
        unsigned int entity_size;
        char entity_value = 0;
        if (!value)
            goto invalid;
        if (value >= 0x80U)
        {
            unsigned int index;
            if (!dluma_loading_utf8_sequence(
                    (const unsigned char *)input + read,
                    input_size - read, &sequence, &codepoint))
                goto invalid;
            if (codepoint == 0x2028U || codepoint == 0x2029U)
            {
                read += sequence;
                if (!dluma_loading_append_newline(
                        output, output_capacity, &write))
                    goto invalid;
                continue;
            }
            if (dluma_loading_codepoint_is_control(codepoint))
            {
                read += sequence;
                continue;
            }
            if (write + sequence >= output_capacity)
                goto invalid;
            for (index = 0; index < sequence; ++index)
                output[write++] = input[read++];
            continue;
        }
        if ((value == '[' &&
             dluma_loading_markup_end(
                 input, input_size, read, ']', &markup_end)) ||
            (value == '<' &&
             dluma_loading_markup_end(
                 input, input_size, read, '>', &markup_end)))
        {
            int closing = 0;
            DlumaLoadingMarkupKind kind =
                dluma_loading_markup_kind(
                    input, read, markup_end, &closing);
            if (kind != DLUMA_LOADING_MARKUP_NONE)
            {
                if (kind == DLUMA_LOADING_MARKUP_BREAK ||
                    (kind == DLUMA_LOADING_MARKUP_BLOCK &&
                     (closing ||
                      (write && output[write - 1U] != '\n'))))
                {
                    if (!dluma_loading_append_newline(
                            output, output_capacity, &write))
                        goto invalid;
                }
                else if (kind == DLUMA_LOADING_MARKUP_LIST_ITEM)
                {
                    if (closing)
                    {
                        if (!dluma_loading_append_newline(
                                output, output_capacity, &write))
                            goto invalid;
                    }
                    else
                    {
                        if (write && output[write - 1U] != '\n' &&
                            !dluma_loading_append_newline(
                                output, output_capacity, &write))
                            goto invalid;
                        if (!dluma_loading_append_byte(
                                output, output_capacity, &write, '-') ||
                            !dluma_loading_append_byte(
                                output, output_capacity, &write, ' '))
                            goto invalid;
                    }
                }
                read = markup_end + 1U;
                continue;
            }
        }
        entity_size = value == '&'
            ? dluma_loading_entity(
                input + read, input_size - read, &entity_value)
            : 0U;
        if (entity_size)
        {
            if (!dluma_loading_append_byte(
                    output, output_capacity, &write, entity_value))
                goto invalid;
            read += entity_size;
            continue;
        }
        ++read;
        if (value == '\r')
        {
            if (read < input_size && input[read] == '\n')
                ++read;
            if (!dluma_loading_append_newline(
                    output, output_capacity, &write))
                goto invalid;
        }
        else if (value == '\n')
        {
            if (!dluma_loading_append_newline(
                    output, output_capacity, &write))
                goto invalid;
        }
        else if (value == '\t')
        {
            if (!dluma_loading_append_byte(
                    output, output_capacity, &write, ' '))
                goto invalid;
        }
        else if (value >= 0x20U && value != 0x7FU)
        {
            if (!dluma_loading_append_byte(
                    output, output_capacity, &write, (char)value))
                goto invalid;
        }
    }
    while (write &&
           (output[write - 1U] == ' ' ||
            output[write - 1U] == '\t' ||
            output[write - 1U] == '\r' ||
            output[write - 1U] == '\n'))
        --write;
    output[write] = 0;
    return 1;
invalid:
    output[0] = 0;
    return 0;
}

int dluma_custom_map_loading_cover_crop(
    unsigned int source_width, unsigned int source_height,
    unsigned int target_width, unsigned int target_height,
    DlumaCustomMapLoadingCrop *out)
{
    unsigned long long source_ratio_left;
    unsigned long long source_ratio_right;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!source_width || !source_height ||
        !target_width || !target_height)
        return 0;
    source_ratio_left =
        (unsigned long long)source_width * target_height;
    source_ratio_right =
        (unsigned long long)source_height * target_width;
    if (source_ratio_left > source_ratio_right)
    {
        unsigned int width = (unsigned int)(
            ((unsigned long long)source_height * target_width) /
            target_height);
        if (!width || width > source_width)
            return 0;
        out->x = (source_width - width) / 2U;
        out->width = width;
        out->height = source_height;
    }
    else
    {
        unsigned int height = (unsigned int)(
            ((unsigned long long)source_width * target_height) /
            target_width);
        if (!height || height > source_height)
            return 0;
        out->y = (source_height - height) / 2U;
        out->width = source_width;
        out->height = height;
    }
    return 1;
}

static void dluma_loading_lexer_skip(DlumaLoadingLexer *lexer)
{
    for (;;)
    {
        while (lexer->cursor < lexer->size)
        {
            char value = lexer->text[lexer->cursor];
            if (value != ' ' && value != '\t' &&
                value != '\r' && value != '\n')
                break;
            ++lexer->cursor;
        }
        if (lexer->cursor + 1U < lexer->size &&
            lexer->text[lexer->cursor] == '/' &&
            lexer->text[lexer->cursor + 1U] == '/')
        {
            lexer->cursor += 2U;
            while (lexer->cursor < lexer->size &&
                   lexer->text[lexer->cursor] != '\r' &&
                   lexer->text[lexer->cursor] != '\n')
                ++lexer->cursor;
            continue;
        }
        if (lexer->cursor + 1U < lexer->size &&
            lexer->text[lexer->cursor] == '/' &&
            lexer->text[lexer->cursor + 1U] == '*')
        {
            lexer->cursor += 2U;
            while (lexer->cursor + 1U < lexer->size &&
                   !(lexer->text[lexer->cursor] == '*' &&
                     lexer->text[lexer->cursor + 1U] == '/'))
                ++lexer->cursor;
            if (lexer->cursor + 1U < lexer->size)
                lexer->cursor += 2U;
            else
                lexer->cursor = lexer->size;
            continue;
        }
        break;
    }
}

static DlumaLoadingToken dluma_loading_lexer_next(DlumaLoadingLexer *lexer)
{
    DlumaLoadingToken token;
    unsigned int write = 0;
    memset(&token, 0, sizeof(token));
    dluma_loading_lexer_skip(lexer);
    if (lexer->cursor >= lexer->size)
    {
        token.kind = DLUMA_LOADING_TOKEN_END;
        return token;
    }
    if ((lexer->text[lexer->cursor] >= 'A' &&
         lexer->text[lexer->cursor] <= 'Z') ||
        (lexer->text[lexer->cursor] >= 'a' &&
         lexer->text[lexer->cursor] <= 'z') ||
        lexer->text[lexer->cursor] == '_')
    {
        token.kind = DLUMA_LOADING_TOKEN_IDENTIFIER;
        while (lexer->cursor < lexer->size)
        {
            char value = lexer->text[lexer->cursor];
            if (!((value >= 'A' && value <= 'Z') ||
                  (value >= 'a' && value <= 'z') ||
                  (value >= '0' && value <= '9') ||
                  value == '_'))
                break;
            if (write + 1U >= DLUMA_ARRAY_COUNT(token.text))
                token.kind = DLUMA_LOADING_TOKEN_INVALID;
            else
                token.text[write++] = value;
            ++lexer->cursor;
        }
        token.text[write < DLUMA_ARRAY_COUNT(token.text)
                       ? write
                       : DLUMA_ARRAY_COUNT(token.text) - 1U] = 0;
        return token;
    }
    if (lexer->text[lexer->cursor] >= '0' &&
        lexer->text[lexer->cursor] <= '9')
    {
        unsigned long long number = 0;
        token.kind = DLUMA_LOADING_TOKEN_NUMBER;
        while (lexer->cursor < lexer->size &&
               lexer->text[lexer->cursor] >= '0' &&
               lexer->text[lexer->cursor] <= '9')
        {
            number = number * 10ULL +
                (unsigned int)(lexer->text[lexer->cursor] - '0');
            if (number > 0xFFFFFFFFULL)
                token.kind = DLUMA_LOADING_TOKEN_INVALID;
            ++lexer->cursor;
        }
        token.number = (unsigned int)number;
        return token;
    }
    if (lexer->text[lexer->cursor] == '"')
    {
        token.kind = DLUMA_LOADING_TOKEN_STRING;
        ++lexer->cursor;
        while (lexer->cursor < lexer->size &&
               lexer->text[lexer->cursor] != '"')
        {
            char value = lexer->text[lexer->cursor++];
            if (value == '\\' && lexer->cursor < lexer->size)
            {
                char escaped = lexer->text[lexer->cursor++];
                if (escaped == '"' || escaped == '\\')
                    value = escaped;
                else
                {
                    token.kind = DLUMA_LOADING_TOKEN_INVALID;
                    break;
                }
            }
            if ((unsigned char)value < 0x20U ||
                write + 1U >= DLUMA_ARRAY_COUNT(token.text))
            {
                token.kind = DLUMA_LOADING_TOKEN_INVALID;
                break;
            }
            token.text[write++] = value;
        }
        if (lexer->cursor >= lexer->size ||
            lexer->text[lexer->cursor] != '"')
            token.kind = DLUMA_LOADING_TOKEN_INVALID;
        else
            ++lexer->cursor;
        token.text[write] = 0;
        return token;
    }
    token.kind = DLUMA_LOADING_TOKEN_PUNCTUATION;
    token.punctuation = lexer->text[lexer->cursor++];
    return token;
}

static int dluma_loading_expect_punctuation(
    DlumaLoadingLexer *lexer, char punctuation)
{
    DlumaLoadingToken token = dluma_loading_lexer_next(lexer);
    return token.kind == DLUMA_LOADING_TOKEN_PUNCTUATION &&
        token.punctuation == punctuation;
}

static int dluma_loading_skip_parenthesized(DlumaLoadingLexer *lexer)
{
    unsigned int depth = 1U;
    while (depth)
    {
        DlumaLoadingToken token = dluma_loading_lexer_next(lexer);
        if (token.kind == DLUMA_LOADING_TOKEN_END ||
            token.kind == DLUMA_LOADING_TOKEN_INVALID)
            return 0;
        if (token.kind != DLUMA_LOADING_TOKEN_PUNCTUATION)
            continue;
        if (token.punctuation == '(')
            ++depth;
        else if (token.punctuation == ')')
            --depth;
    }
    return 1;
}

static int dluma_loading_copy_token_text(
    char *destination, unsigned int capacity, const char *source)
{
    unsigned int index = 0;
    if (!destination || !capacity || !source)
        return 0;
    while (source[index])
    {
        if (index + 1U >= capacity)
        {
            destination[0] = 0;
            return 0;
        }
        destination[index] = source[index];
        ++index;
    }
    destination[index] = 0;
    return 1;
}

static int dluma_loading_parse_alias(
    DlumaLoadingLexer *lexer, const char *kind,
    const char *selected_alias, const char *texture,
    unsigned int texture_width, unsigned int texture_height,
    DlumaCustomMapLoadingTextureDefinition *candidate,
    unsigned int *match_count)
{
    DlumaLoadingToken alias;
    DlumaLoadingToken values[4];
    unsigned int index;
    unsigned int value_count =
        dluma_loading_text_equal(kind, "rect") ? 4U : 0U;
    if (!dluma_loading_expect_punctuation(lexer, '('))
        return 0;
    alias = dluma_loading_lexer_next(lexer);
    if (alias.kind != DLUMA_LOADING_TOKEN_STRING)
        return 0;
    for (index = 0; index < value_count; ++index)
    {
        if (!dluma_loading_expect_punctuation(lexer, ','))
            return 0;
        values[index] = dluma_loading_lexer_next(lexer);
        if (values[index].kind != DLUMA_LOADING_TOKEN_NUMBER)
            return 0;
    }
    if (!dluma_loading_expect_punctuation(lexer, ')'))
        return 0;
    if (!dluma_loading_text_equal(alias.text, selected_alias))
        return 1;
    ++*match_count;
    if (*match_count != 1U ||
        !texture_width || !texture_height ||
        texture_width > DLUMA_LOADING_DEF_MAX_DIMENSION ||
        texture_height > DLUMA_LOADING_DEF_MAX_DIMENSION ||
        !dluma_loading_copy_token_text(
            candidate->texture,
            DLUMA_ARRAY_COUNT(candidate->texture), texture) ||
        !dluma_loading_copy_token_text(
            candidate->alias,
            DLUMA_ARRAY_COUNT(candidate->alias), alias.text))
        return 0;
    candidate->texture_width = texture_width;
    candidate->texture_height = texture_height;
    if (!value_count)
    {
        candidate->right = texture_width;
        candidate->bottom = texture_height;
        candidate->whole = 1U;
        return 1;
    }
    candidate->left = values[0].number;
    candidate->top = values[1].number;
    candidate->right = values[2].number;
    candidate->bottom = values[3].number;
    if (candidate->left >= candidate->right ||
        candidate->top >= candidate->bottom ||
        candidate->right > texture_width ||
        candidate->bottom > texture_height)
        return 0;
    return 1;
}

int dluma_custom_map_loading_parse_texture_definition(
    const char *text, unsigned int size, const char *selected_alias,
    DlumaCustomMapLoadingTextureDefinition *out)
{
    DlumaLoadingLexer lexer;
    DlumaCustomMapLoadingTextureDefinition candidate;
    unsigned int match_count = 0;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    memset(&candidate, 0, sizeof(candidate));
    if (!text || !size || !selected_alias || !selected_alias[0])
        return 0;
    lexer.text = text;
    lexer.size = size;
    lexer.cursor = 0;
    for (;;)
    {
        DlumaLoadingToken token = dluma_loading_lexer_next(&lexer);
        if (token.kind == DLUMA_LOADING_TOKEN_END)
            break;
        if (token.kind == DLUMA_LOADING_TOKEN_INVALID)
            return 0;
        if (token.kind == DLUMA_LOADING_TOKEN_PUNCTUATION &&
            token.punctuation == '!')
        {
            token = dluma_loading_lexer_next(&lexer);
            if (token.kind != DLUMA_LOADING_TOKEN_IDENTIFIER ||
                !dluma_loading_expect_punctuation(&lexer, '(') ||
                !dluma_loading_skip_parenthesized(&lexer))
                return 0;
            continue;
        }
        if (token.kind != DLUMA_LOADING_TOKEN_IDENTIFIER ||
            !dluma_loading_text_equal(token.text, "texture"))
            return 0;
        {
            DlumaLoadingToken texture;
            DlumaLoadingToken width;
            DlumaLoadingToken height;
            if (!dluma_loading_expect_punctuation(&lexer, '('))
                return 0;
            texture = dluma_loading_lexer_next(&lexer);
            if (texture.kind != DLUMA_LOADING_TOKEN_STRING ||
                !dluma_loading_expect_punctuation(&lexer, ','))
                return 0;
            width = dluma_loading_lexer_next(&lexer);
            if (width.kind != DLUMA_LOADING_TOKEN_NUMBER ||
                !dluma_loading_expect_punctuation(&lexer, ','))
                return 0;
            height = dluma_loading_lexer_next(&lexer);
            if (height.kind != DLUMA_LOADING_TOKEN_NUMBER ||
                !dluma_loading_expect_punctuation(&lexer, ')') ||
                !dluma_loading_expect_punctuation(&lexer, '{'))
                return 0;
            for (;;)
            {
                DlumaLoadingToken directive =
                    dluma_loading_lexer_next(&lexer);
                if (directive.kind == DLUMA_LOADING_TOKEN_INVALID ||
                    directive.kind == DLUMA_LOADING_TOKEN_END)
                    return 0;
                if (directive.kind == DLUMA_LOADING_TOKEN_PUNCTUATION &&
                    directive.punctuation == '}')
                    break;
                if (directive.kind != DLUMA_LOADING_TOKEN_IDENTIFIER ||
                    !dluma_loading_expect_punctuation(&lexer, '('))
                    return 0;
                if (dluma_loading_text_equal(directive.text, "whole") ||
                    dluma_loading_text_equal(directive.text, "rect"))
                {
                    lexer.cursor -= 1U;
                    if (!dluma_loading_parse_alias(
                            &lexer, directive.text, selected_alias,
                            texture.text, width.number, height.number,
                            &candidate, &match_count))
                        return 0;
                }
                else if (!dluma_loading_skip_parenthesized(&lexer))
                    return 0;
            }
        }
    }
    if (match_count != 1U)
        return 0;
    *out = candidate;
    return 1;
}

static int dluma_loading_device_leaf(const char *leaf, unsigned int length)
{
    static const char *devices[] = {
        "con", "prn", "aux", "nul", "clock$",
        "com1", "com2", "com3", "com4", "com5",
        "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
        "lpt6", "lpt7", "lpt8", "lpt9"};
    unsigned int device;
    unsigned int base_length = 0;
    while (base_length < length && leaf[base_length] != '.')
        ++base_length;
    for (device = 0; device < DLUMA_ARRAY_COUNT(devices); ++device)
    {
        unsigned int index = 0;
        while (devices[device][index] &&
               index < base_length &&
               dluma_loading_lower(devices[device][index]) ==
                   dluma_loading_lower(leaf[index]))
            ++index;
        if (index == base_length && !devices[device][index])
            return 1;
    }
    return 0;
}

int dluma_custom_map_loading_resource_path_valid(
    const char *path, unsigned int capacity)
{
    unsigned int length = 0;
    unsigned int component = 0;
    if (!path || !capacity)
        return 0;
    while (length < capacity && path[length])
        ++length;
    if (!length || length == capacity ||
        path[0] == '\\' || path[0] == '/' ||
        path[length - 1U] == '\\' ||
        path[length - 1U] == '/' ||
        (length > 1U && path[1] == ':'))
        return 0;
    while (component < length)
    {
        unsigned int end = component;
        while (end < length &&
               path[end] != '\\' && path[end] != '/')
        {
            unsigned char value = (unsigned char)path[end];
            if (value < 0x20U || value == ':' || value == '*' ||
                value == '?' || value == '"' || value == '<' ||
                value == '>' || value == '|')
                return 0;
            ++end;
        }
        if (end == component ||
            (end - component == 1U && path[component] == '.') ||
            (end - component == 2U && path[component] == '.' &&
             path[component + 1U] == '.') ||
            path[end - 1U] == ' ' || path[end - 1U] == '.' ||
            dluma_loading_device_leaf(path + component, end - component))
            return 0;
        component = end + 1U;
    }
    return 1;
}
