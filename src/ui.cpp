// ==============================
// Includes
// ==============================

#include "ui.h"
#include "mp.h"
#include "config.h"
#include <filesystem>
#include <cstring>
#include <imgui.h>
#include <unordered_map>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_freetype.h>
#include <portable-file-dialogs.h>
#include <iostream>
#include <algorithm>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <optional>
#include <queue>
#include <vector>
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

// =============================
// Global Constants, Variables, Types
// =============================

#define SEARCH_RESULTS_PER_PAGE 20
#define STRING_LENGTH 512

#define SMALL_IMAGE_SIZE ImVec2(64, 64)
#define LARGE_IMAGE_SIZE ImVec2(210, 210)

#define INVISIBILE ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
#define BLACK ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
#define WHITE ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
#define DARK_GRAY ImVec4(0.2f, 0.2f, 0.2f, 1.0f)
#define GRAY ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
#define LIGHT_GRAY ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
#define RED ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
#define ORANGE ImVec4(1.0f, 0.5f, 0.0f, 1.0f)
#define GREEN ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
#define BLUE ImVec4(0.0f, 0.0f, 1.0f, 1.0f)
#define YELLOW ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
#define PURPLE ImVec4(1.0f, 0.0f, 1.0f, 1.0f)
#define CYAN ImVec4(0.0f, 1.0f, 1.0f, 1.0f)
#define SELECTED_COLOR_1 RED
#define SELECTED_COLOR_2 GREEN
#define UNSELECTED_COLOR WHITE
#define EMPTY_STR "<Empty>"

#define UV0 ImVec2(0.0f, 0.0f)
#define UV1 ImVec2(1.0f, 1.0f)

#define UV0_INVERTED_HORIZONTALLY ImVec2(1.0f, 0.0f)
#define UV1_INVERTED_HORIZONTALLY ImVec2(0.0f, 1.0f)

#define DEFAULT_MARGIN 10.0f

enum ViewEnum {
    SHOW_RIGHT_NONE,
    SHOW_RIGHT_QUEUE,
    SHOW_CENTER_NONE,
    SHOW_CENTER_HOME,
    SHOW_CENTER_SETTINGS,
    SHOW_CENTER_EDIT,
    SHOW_CENTER_SONG,
    SHOW_CENTER_ALBUM,
    SHOW_CENTER_PLAYLIST,
    SHOW_CENTER_ARTIST,
    SHOW_CENTER_LYRICS,
    SHOW_CENTER_SEARCH
};

enum SearchResultEnum {
    SEARCH_TYPE_SONG,
    SEARCH_TYPE_ALBUM,
    SEARCH_TYPE_ARTIST,
    SEARCH_TYPE_PLAYLIST
};

enum SongEditEnum {
    SHOW_NONE,
    SHOW_LYRICS,
    SHOW_EDIT_LYRICS,
    SHOW_EDIT,
    SHOW_ALBUM_PICKER,
    SHOW_ARTIST_PICKER,
    SHOW_ARTIST_MERGE,
};

enum EditType {
    EDIT_NONE,
    EDIT_SONG_NAME,
    EDIT_SONG_YEAR,
    EDIT_SONG_TRACK,
    EDIT_ALBUM_NAME,
    EDIT_ALBUM_YEAR,
    EDIT_ARTIST_NAME,
    EDIT_PLAYLIST_NAME
};

struct GLTexture {
    GLuint id;
    ImVec2 uv0;
    ImVec2 uv1;
};

using SlotID = int;

struct Slot {
    GLuint tex;
    SlotID idx;
    int xoff;
    int yoff;
};

bool operator==(Slot& s1, Slot& s2)
{
    return s1.tex == s2.tex && s1.idx == s2.idx && s1.xoff == s2.xoff && s1.yoff == s2.yoff;
}

bool operator!=(Slot& s1, Slot& s2)
{
    return !(s1 == s2);
}

struct UIContext {
    GLFWwindow* window;

    struct TextureInfo {
        using TextureSlotID = int;
        // create TEXTURE_SIZE x TEXTURE_SIZE textures that have pages for LARGE_COVER_SIZE x LARGE_COVER_SIZE
        // images to upload. 

        // GL texture objects that contain the slots
        std::vector<GLuint> textures;
        // Use PQ to acquire a slot idx that is drawn to in create_texture
        std::priority_queue<SlotID, std::vector<SlotID>, std::greater<SlotID>> free_slot_textures;
        // Use to keep track of the number of slots free_slot_textures has handed out
        int num_slots;
        // Use to map a mp entity to a slot
        std::unordered_map<MPUID, Slot> slot_map;
        // Slot for the default album art
        Slot default_album_slot;
        Slot default_artist_slot;
        // Slot for editing
        Slot edit_slot;

        GLTexture play_button;
        GLTexture skip_button;
        GLTexture pause_button;
        GLTexture repeat_button;
        GLTexture shuffle_button;
        GLTexture autoplay_button;
        GLTexture show_queue_button;
        GLTexture menu_button;
        GLTexture pin_button;
        GLTexture home_button;
        GLTexture pencil_button;
        GLTexture settings_button;
        GLTexture lyrics_button;
        GLTexture close_button;
        GLTexture maximize_button;
        GLTexture round_rewind_button;
        GLTexture mixer_button;
        GLTexture search;
        GLTexture volume;
        GLTexture mute;
    } textures;

    struct Debug {
        double fps;
    } debug;

    bool show_demo_window;
    bool show_debug_window;

    bool quick_access_open = true;
    bool queue_open = true;

    struct {
        ViewEnum type;
        LruCacheRef<Song> song;
        LruCacheRef<Album> album;
        LruCacheRef<Artist> artist;
        LruCacheRef<Playlist> playlist;
        bool pinned;

        // if type is album or playlist, then write to song_tracks
        // otherwise, write to songs
        std::vector<SongAlbumArtistTrack> song_tracks;
        std::vector<LruCacheRef<Song>> songs;
        std::vector<LruCacheRef<Album>> albums;

        struct {
            char song[512];
            char artist[512];
            char album[512];
            char playlist[512];
            char song_path[512];
            char song_lyrics[MAX_LYRICS_SIZE];
            int year;
            int track;
            ArtistID artist_id;
            AlbumID album_id;
            Art art;

            // this actually stores the volume before any edits so
            // that the volume may be reset
            float volume;

            bool changed_info;
            bool changed_art;
            bool reset_art;

            EditType edit_type;

            int added_songs_search_type;
        } edit;

        SongEditEnum show;
        SongEditEnum show_split;

        char song_lyrics[MAX_LYRICS_SIZE];

        SearchResultEnum search_result_type;
        SearchResult<SongAlbumArtist> song_search_result;
        SearchResult<LruCacheRef<Album>> album_search_result;
        SearchResult<LruCacheRef<Artist>> artist_search_result;
        SearchResult<LruCacheRef<Playlist>> playlist_search_result;

        SearchResult<SongAlbumArtist> added_songs;
        SearchResult<LruCacheRef<Album>> added_albums;
        SearchResult<LruCacheRef<Artist>> added_artists;
    } center;

    std::deque<std::pair<ViewEnum, GenericID>> view_history;

    char search_query[256];

    bool window_maximized;

    // for file dialogs
    std::optional<std::future<std::string>> prompt;
    std::function<void()> prompt_callback;
};

static UIContext ctx;

// =============================
// ImGUI Helper Functions
// =============================

enum AlignmentEnum {
    ALIGN_TOP = 0,
    ALIGN_LEFT = 0,
    ALIGN_CENTER = 1,
    ALIGN_RIGHT = 2,
    ALIGN_BOTTOM = 2
};

enum MenuPopupEnum {
    MENU_END_POPUP,
    MENU_CLOSE_POPUP,
    MENU_CLOSE_TABLE
};

enum ItemActiveEnum {
    ITEM_INACTIVE,
    ITEM_ACTIVE,
    ITEM_ACTIVATED,
    ITEM_DEACTIVATED,
};

struct CustomTableParams {
    int num_rows;
    int num_cols;
    int* selected_row;
    float header_row_height;
    float row_height;
    float table_width;
    std::vector<float*> col_offsets;
    std::vector<float> col_widths;
    std::vector<std::function<void(ImVec2, ImVec2)>> header_row_col_callback;
    std::vector<std::function<void(ImVec2, ImVec2, int)>> body_row_col_callback;
    std::function<MenuPopupEnum(int*)> menu_popup_callback;
};

static void imgui_push_font_scale(float scale)
{
    ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * scale);
}

static void imgui_pop_font_scale()
{
    ImGui::PopFont();
}

static void imgui_separator()
{
    ImVec2 position = ImGui::GetCursorPos();
    ImGui::Separator();
    ImGui::SetCursorPos(position);
}

static bool imgui_button(const char* text)
{
    const bool pressed = ImGui::Button(text);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

static bool imgui_text_button(const char* text)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    ImVec2 cursor_pos = ImGui::GetCursorPos();
    ImGui::PushStyleColor(ImGuiCol_Text, BLACK);
    ImGui::Text("%s", text);
    ImGui::PopStyleColor();
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (hovered) ImGui::PushStyleColor(ImGuiCol_Text, LIGHT_GRAY);
    ImGui::SetCursorPos(cursor_pos);
    ImGui::Text("%s", text);
    if (hovered) ImGui::PopStyleColor();

    ImGui::PopStyleVar();
    return hovered && clicked;
}

// Taken from imgui
static int get_utf8_char(unsigned int* out_char, const char* in_text, const char* in_text_end)
{
    static const char lengths[32] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0 };
    static const int masks[]  = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
    static const uint32_t mins[] = { 0x400000, 0, 0x80, 0x800, 0x10000 };
    static const int shiftc[] = { 0, 18, 12, 6, 0 };
    static const int shifte[] = { 0, 6, 4, 2, 0 };
    int len = lengths[*(const unsigned char*)in_text >> 3];
    int wanted = len + (len ? 0 : 1);

    if (in_text_end == NULL)
        in_text_end = in_text + wanted;

    unsigned char s[4];
    s[0] = in_text + 0 < in_text_end ? in_text[0] : 0;
    s[1] = in_text + 1 < in_text_end ? in_text[1] : 0;
    s[2] = in_text + 2 < in_text_end ? in_text[2] : 0;
    s[3] = in_text + 3 < in_text_end ? in_text[3] : 0;

    *out_char  = (uint32_t)(s[0] & masks[len]) << 18;
    *out_char |= (uint32_t)(s[1] & 0x3f) << 12;
    *out_char |= (uint32_t)(s[2] & 0x3f) <<  6;
    *out_char |= (uint32_t)(s[3] & 0x3f) <<  0;
    *out_char >>= shiftc[len];

    int e = 0;
    e  = (*out_char < mins[len]) << 6;
    e |= ((*out_char >> 11) == 0x1b) << 7;
    e |= (*out_char > IM_UNICODE_CODEPOINT_MAX) << 8;
    e |= (s[1] & 0xc0) >> 2;
    e |= (s[2] & 0xc0) >> 4;
    e |= (s[3]       ) >> 6;
    e ^= 0x2a;
    e >>= shifte[len];

    if (e)
    {
        wanted = (std::min)(wanted, static_cast<int>(!!s[0] + !!s[1] + !!s[2] + !!s[3]));
        *out_char = IM_UNICODE_CODEPOINT_INVALID;
    }

    return wanted;
}

static bool imgui_aligned_text(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align = ALIGN_LEFT, AlignmentEnum v_align = ALIGN_TOP, bool button = false)
{
    const char* start = text;
    const char* end = text + strlen(text);
    const char ellipse[] = "…";
    ImVec2 ellipse_size = ImGui::CalcTextSize(ellipse);

    ImVec2 text_size(0.0f, ellipse_size.y);
    ImFont* font = ImGui::GetFont();
    ImFontBaked* baked = font->GetFontBaked(ImGui::GetFontSize());

    while (start < end)
    {
        unsigned int c;
        int wanted = get_utf8_char(&c, start, end);
        const float char_width = (c < (unsigned int)baked->IndexAdvanceX.Size) ? baked->IndexAdvanceX.Data[c] : -1.0f;

        if (text_size.x + char_width + ellipse_size.x >= region_size.x)
            break;

        text_size.x += char_width;
        start += wanted;
    }

    if (start == text && ellipse_size.x >= region_size.x)
    {
        ImGui::Dummy(text_size);
        return false;
    }

    if (start != end)
        text_size.x += ellipse_size.x;

    ImGui::SetCursorPos(ImVec2(
                region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));

    char new_text[1024];
    const char* rendered_text = text;
    if (start < end)
    {
        // buffer overflow lol
        memcpy(new_text, text, start - text);
        memcpy(new_text + static_cast<size_t>(start - text), ellipse, std::size(ellipse));
        new_text[start - text + std::size(ellipse)] = '\0';
        rendered_text = new_text;
    }

    if (button)
        return imgui_text_button(rendered_text);

    ImGui::Text("%s", rendered_text);
    return false;
}

static bool imgui_image_button(ImTextureRef tex_ref, const ImVec2& image_size, const ImVec4& tint_col = WHITE, const ImVec2& uv0 = UV0, const ImVec2& uv1 = UV1)
{
    ImGui::ImageWithBg(tex_ref, image_size, uv0, uv1, INVISIBILE, tint_col);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return ImGui::IsItemClicked();
}

static bool imgui_aligned_text_button(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align = ALIGN_LEFT, AlignmentEnum v_align = ALIGN_TOP)
{
    return imgui_aligned_text(text, region_start, region_size, h_align, v_align, true);
}

static bool imgui_is_mouse_in_window_bounds()
{
    const ImVec2 mouse_pos = ImGui::GetMousePos();
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    return mouse_pos.x >= window_pos.x
        && mouse_pos.x <= window_pos.x + window_size.x
        && mouse_pos.y >= window_pos.y
        && mouse_pos.y <= window_pos.y + window_size.y;
}

static bool imgui_is_region_hovered(ImVec2 region_start, ImVec2 size)
{
    const ImVec2 mouse_pos = ImGui::GetMousePos();
    return imgui_is_mouse_in_window_bounds()
        && mouse_pos.x >= region_start.x 
        && mouse_pos.x <= region_start.x + size.x
        && mouse_pos.y >= region_start.y 
        && mouse_pos.y <= region_start.y + size.y;
}

static void imgui_custom_table(CustomTableParams& params)
{
    constexpr float separator_width = 8.0f;
    constexpr float separator_line_width = 2.0f;
    constexpr float separator_offset = -12.0f;
    ImVec2 origin = ImGui::GetCursorPos();
    ImVec2 origin_screen = ImGui::GetCursorScreenPos();
    imgui_separator();

    params.col_offsets.push_back(&params.table_width);
    for (size_t i = 0; i < params.col_offsets.size() - 1; i++)
    {
        *params.col_offsets[i] = (std::min)(*params.col_offsets[i], *params.col_offsets[i+1]);
        *params.col_offsets[i] = (std::max)(*params.col_offsets[i], 0.0f);
    }

    for (size_t i = 0; i < params.col_offsets.size() - 1; i++)
    {
        ImGui::PushID(i);
        ImVec2 region_start(origin.x + *params.col_offsets[i], origin.y);
        ImVec2 region_size(*params.col_offsets[i+1] - *params.col_offsets[i] + 2 * separator_offset - separator_line_width, params.row_height);
        if (i != params.col_offsets.size() - 2)
        {
            ImGui::SetCursorPos(ImVec2(origin.x + *params.col_offsets[i+1] + separator_offset - separator_width / 2.0f, origin.y));
            ImGui::InvisibleButton("##", ImVec2(separator_width, params.header_row_height));
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                *params.col_offsets[i+1] = std::clamp(
                        ImGui::GetMousePos().x - origin_screen.x - separator_offset + separator_line_width / 2.0f,
                        *params.col_offsets[i], *params.col_offsets[i+2]);
            }
            ImGui::GetWindowDrawList()->AddLineV(
                    origin_screen.x + *params.col_offsets[i+1] + separator_offset - separator_line_width / 2.0f, 
                    origin_screen.y + 5.0f, 
                    origin_screen.y + params.header_row_height - 5.0f, 
                    IM_COL32(56, 56, 56, 255),
                    separator_line_width);
        }
        region_start = ImVec2(origin.x + *params.col_offsets[i], origin.y);
        region_size = ImVec2(*params.col_offsets[i+1] - *params.col_offsets[i] + 2 * separator_offset - separator_line_width, params.header_row_height);
        ImGui::SetCursorPos(region_start);
        params.header_row_col_callback[i](region_start, region_size);
        ImGui::PopID();
    }
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + params.header_row_height));
    imgui_separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    ImGui::BeginChild("##table_body", ImGui::GetContentRegionAvail(), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    float cursor_pos_y = ImGui::GetCursorPosY();
    for (int row = 0; row < params.num_rows; row++)
    {
        ImGui::PushID(row);

        ImVec2 row_size = ImVec2(params.table_width, params.row_height);
        ImVec2 mouse_pos = ImGui::GetMousePos();

        ImGui::SetCursorPos(ImVec2(0.0f, cursor_pos_y));
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + row_size.x, p_min.y + row_size.y);

        bool in_bounds = mouse_pos.x >= p_min.x && mouse_pos.x < p_max.x && mouse_pos.y >= p_min.y && mouse_pos.y < p_max.y;

        if (*params.selected_row == row || (*params.selected_row == -1 && in_bounds))
        {
            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(0x20, 0x20, 0x20, 0xFF));
        }

        ImGui::Dummy(row_size);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            *params.selected_row = row;
        if (ImGui::BeginPopupContextItem("A"))
        {
            *params.selected_row = row;
            MenuPopupEnum res = params.menu_popup_callback(params.selected_row);
            if (res == MENU_CLOSE_TABLE)
            {
                ImGui::EndPopup();
                ImGui::PopID();
                ImGui::EndChild();
                return;
            }
            else if (res == MENU_CLOSE_POPUP)
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            else if (res == MENU_END_POPUP)
            {
                ImGui::EndPopup();
            }
        }
        else if (*params.selected_row == row)
        {
            *params.selected_row = -1;
        }

        for (int col = 0; col < params.num_cols; col++)
        {
            ImGui::PushID(col);
            ImVec2 region_start(*params.col_offsets[col], cursor_pos_y);
            ImVec2 region_size(*params.col_offsets[col+1] - *params.col_offsets[col] + 2 * separator_offset - separator_line_width, params.row_height);
            ImGui::SetCursorPos(region_start);
            params.body_row_col_callback[col](region_start, region_size, row);
            ImGui::PopID();
        }
        ImGui::PopID();
        cursor_pos_y += params.row_height;
    }
    ImGui::EndChild();
}

static ItemActiveEnum imgui_custom_slider_float(const char* label, float* value, float min, float max, ImVec2 size)
{
    (void)value; (void)min; (void)max;
    ImVec2 origin = ImGui::GetCursorPos();
    ImVec2 origin_screen = ImGui::GetCursorScreenPos();
    float diff = max - min;

    float value_x;
    if (diff > 0.0f)
        value_x = origin_screen.x + (*value - min) / diff * size.x;
    else
        value_x = origin_screen.x + min;

    if (value_x - origin_screen.x > 1e-5)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(origin_screen.x, origin_screen.y),
                ImVec2(value_x, origin_screen.y + size.y),
                IM_COL32(0, 255, 0, 255));
    }

    if (origin_screen.x + size.x - value_x > 1e-5)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(value_x, origin_screen.y),
                ImVec2(origin_screen.x + size.x, origin_screen.y + size.y),
                IM_COL32(255, 0, 0, 255));
    }

    float button_padding = 5.0f;
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y));

    ImGui::InvisibleButton(label, ImVec2(size.x, size.y + 2 * button_padding));

    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsItemActive())
    {
        *value = std::clamp((ImGui::GetMousePos().x - origin_screen.x) / size.x * diff + min, min, max);
    }

    if (ImGui::IsItemDeactivated())
        return ITEM_DEACTIVATED;
    if (ImGui::IsItemActivated())
        return ITEM_ACTIVATED;
    if (ImGui::IsItemActive())
        return ITEM_ACTIVE;
    return ITEM_INACTIVE;
}

// =============================
// ImGUI Helper Functions End
// =============================

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static bool ui_key_callback(int key, int scancode, int action, int mods)
{
    (void)key; (void)scancode; (void)action; (void)mods;
    return ImGui::GetIO().WantCaptureKeyboard;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window;
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
        ctx.show_demo_window = !ctx.show_demo_window;
    if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
        ctx.show_debug_window = !ctx.show_debug_window;
    if (ui_key_callback(key, scancode, action, mods))
        return;
}

static GLTexture get_texture_from_slot(Slot& slot)
{
    return GLTexture{ 
        .id = slot.tex,
        .uv0 = ImVec2(
                static_cast<float>(slot.xoff) / TEXTURE_SIZE, 
                static_cast<float>(slot.yoff) / TEXTURE_SIZE),
        .uv1 = ImVec2(
                static_cast<float>(slot.xoff + ART_WIDTH) / TEXTURE_SIZE, 
                static_cast<float>(slot.yoff + ART_WIDTH) / TEXTURE_SIZE)
    };
}

static void copy_texture_slot(Slot& dst, Slot& src)
{
    glCopyImageSubData(
            src.tex, GL_TEXTURE_2D, 0, src.xoff, src.yoff, 0,
            dst.tex, GL_TEXTURE_2D, 0, dst.xoff, dst.yoff, 0,
            ART_WIDTH, ART_WIDTH, 1
        );
}

static void update_texture_slot(Slot& slot, const uint8_t* art_data)
{
    glBindTexture(GL_TEXTURE_2D, slot.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot.xoff, slot.yoff, ART_WIDTH, ART_WIDTH, GL_RGBA, GL_UNSIGNED_BYTE, art_data);
}

static Slot get_slot()
{
    Slot slot{};
    if (ctx.textures.free_slot_textures.size() == 0)
        ctx.textures.free_slot_textures.push(ctx.textures.num_slots++);
    slot.idx = ctx.textures.free_slot_textures.top();
    ctx.textures.free_slot_textures.pop();
    const size_t texture_idx = slot.idx / SLOTS_PER_TEXTURE;
    assert(texture_idx <= ctx.textures.textures.size());
    if (texture_idx == ctx.textures.textures.size())
    {
        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_SIZE, TEXTURE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        ctx.textures.textures.push_back(id);
    }
    slot.tex = ctx.textures.textures[slot.idx / SLOTS_PER_TEXTURE];

    const int slots_across = TEXTURE_SIZE / ART_WIDTH;
    slot.xoff = (slot.idx % SLOTS_PER_TEXTURE) % slots_across * ART_WIDTH;
    slot.yoff = (slot.idx % SLOTS_PER_TEXTURE) / slots_across * ART_WIDTH;

    return slot;
}

static Slot create_texture(const uint8_t* data)
{
    Slot slot = get_slot();
    update_texture_slot(slot, data);
    return slot;
}

static void insert_art_into_map(MPUID uid)
{
    Art art = mp_get_art(uid);
    if (art == nullptr)
        ctx.textures.slot_map[uid] = (mp_get_uid_type(uid) == MPUID_ARTIST) ? ctx.textures.default_artist_slot : ctx.textures.default_album_slot;
    else
        ctx.textures.slot_map[uid] = create_texture(art.data);
}

static Slot get_slot_from_song(SongID song_id)
{
    MPUID uid = mp_get_song_uid(song_id);
    if (ctx.textures.slot_map.find(uid) == ctx.textures.slot_map.end())
        insert_art_into_map(uid);
    return ctx.textures.slot_map[uid];
}

static GLTexture get_texture_from_song(SongID song_id)
{
    Slot slot = get_slot_from_song(song_id);
    return get_texture_from_slot(slot);
}

static Slot get_slot_from_album(AlbumID album_id)
{
    MPUID uid = mp_get_album_uid(album_id);
    if (ctx.textures.slot_map.find(uid) == ctx.textures.slot_map.end())
        insert_art_into_map(uid);
    return ctx.textures.slot_map[uid];
}

static GLTexture get_texture_from_album(AlbumID album_id)
{
    Slot slot = get_slot_from_album(album_id);
    return get_texture_from_slot(slot);
}

static Slot get_slot_from_artist(ArtistID artist_id)
{
    MPUID uid = mp_get_artist_uid(artist_id);
    if (ctx.textures.slot_map.find(uid) == ctx.textures.slot_map.end())
        insert_art_into_map(uid);
    return ctx.textures.slot_map[uid];
}

static GLTexture get_texture_from_artist(ArtistID artist_id)
{
    Slot slot = get_slot_from_artist(artist_id);
    return get_texture_from_slot(slot);
}

static Slot get_slot_from_playlist(PlaylistID playlist_id)
{
    MPUID uid = mp_get_playlist_uid(playlist_id);
    if (ctx.textures.slot_map.find(uid) == ctx.textures.slot_map.end())
        insert_art_into_map(uid);
    return ctx.textures.slot_map[uid];
}

static GLTexture get_texture_from_playlist(PlaylistID playlist_id)
{
    Slot slot = get_slot_from_playlist(playlist_id);
    return get_texture_from_slot(slot);
}

static bool mpuid_has_valid_texture(MPUID uid)
{
    return ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end() 
        && ctx.textures.slot_map[uid] != ctx.textures.default_album_slot
        && ctx.textures.slot_map[uid] != ctx.textures.default_artist_slot;
}

static void delete_texture(Slot& slot)
{
    ctx.textures.free_slot_textures.push(slot.idx);
}

static void delete_texture_with_uid(MPUID uid)
{
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
    {
        if (mpuid_has_valid_texture(uid))
            delete_texture(ctx.textures.slot_map[uid]);
        ctx.textures.slot_map.erase(ctx.textures.slot_map.find(uid));
    }
}

static void push_history_entry(ViewEnum type, int id)
{
    std::pair<ViewEnum, int> entry{type, id};
    if (ctx.view_history.empty() || ctx.view_history.back() != entry) {
        ctx.view_history.push_back(entry);
        if (ctx.view_history.size() > HISTORY_MAX_SIZE)
            ctx.view_history.pop_front();
    }
}

// this has to be called after the objects have been moved bc those objects
// are often from these containers
static void reset_center_view_containers()
{
    ctx.center.song_search_result.entries.clear();
    ctx.center.album_search_result.entries.clear();
    ctx.center.artist_search_result.entries.clear();
    ctx.center.playlist_search_result.entries.clear();
    ctx.center.added_songs.entries.clear();
    ctx.center.added_albums.entries.clear();
    ctx.center.added_artists.entries.clear();
    ctx.center.song_tracks.clear();
    ctx.center.songs.clear();
    ctx.center.albums.clear();
    ctx.center.song_lyrics[0] = '\0';
    ctx.center.edit.song_lyrics[0] = '\0';
    ctx.center.edit.song[0] = '\0';
    ctx.center.edit.artist[0] = '\0';
    ctx.center.edit.album[0] = '\0';
    ctx.center.edit.playlist[0] = '\0';
    ctx.center.edit.song_path[0] = '\0';
    ctx.center.edit.album_id = INVALID_ID;
    ctx.center.edit.artist_id = INVALID_ID;
    ctx.center.edit.track = INVALID_TRACK;
    ctx.center.edit.year = INVALID_YEAR;
    ctx.center.edit.changed_info = false;
    ctx.center.edit.changed_art = false;
    ctx.center.edit.edit_type = EDIT_NONE;
    ctx.center.show = SHOW_NONE;
    ctx.center.show_split = SHOW_NONE;
    if (ctx.center.edit.art.data != nullptr)
        ctx.center.edit.art.clear();
}

static void set_center_view_lyrics()
{
    ctx.center.type = SHOW_CENTER_LYRICS;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    ctx.center.song = nullptr;
    reset_center_view_containers();
}

static void set_center_view_home()
{
    ctx.center.type = SHOW_CENTER_HOME;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    ctx.center.song = nullptr;
    reset_center_view_containers();
}

static void set_center_view_settings()
{
    ctx.center.type = SHOW_CENTER_SETTINGS;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    ctx.center.song = nullptr;
    reset_center_view_containers();
}

static void refresh_added_songs()
{
    if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_SONG)
    {
        ctx.center.added_songs.entries.clear();
        SearchResult<SongAlbumArtist>& result = ctx.center.added_songs; 
        result.results_per_page = 20;
        result.num_results = mp_ctx.added_songs.size();
        result.num_pages = (result.num_results + result.results_per_page - 1) / result.results_per_page;
        if (result.num_pages == 0)
        {
            result.page_num = 0;
            return;
        }
        result.page_num = std::clamp(result.page_num, 1, result.num_pages);
        for (int i = (result.page_num - 1) * result.results_per_page; i < result.page_num * result.results_per_page && i < result.num_results; i++)
        {
            result.entries.emplace_back(
                        mp_get_song(mp_ctx.added_songs[i]),
                        mp_get_album_from_song(mp_ctx.added_songs[i]),
                        mp_get_artist_from_song(mp_ctx.added_songs[i])
                    );
        }
    }
    else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ALBUM)
    {
        ctx.center.added_albums.entries.clear();
        SearchResult<LruCacheRef<Album>>& result = ctx.center.added_albums; 
        result.results_per_page = 20;
        result.num_results = mp_ctx.added_albums.size();
        result.num_pages = (result.num_results + result.results_per_page - 1) / result.results_per_page;
        if (result.num_pages == 0)
        {
            result.page_num = 0;
            return;
        }
        result.page_num = std::clamp(result.page_num, 1, result.num_pages);
        for (int i = (result.page_num - 1) * result.results_per_page; i < result.page_num * result.results_per_page && i < result.num_results; i++)
            result.entries.emplace_back(mp_get_album(mp_ctx.added_albums[i]));
    }
    else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ARTIST)
    {
        ctx.center.added_artists.entries.clear();
        SearchResult<LruCacheRef<Artist>>& result = ctx.center.added_artists; 
        result.results_per_page = 20;
        result.num_results = mp_ctx.added_artists.size();
        result.num_pages = (result.num_results + result.results_per_page - 1) / result.results_per_page;
        if (result.num_pages == 0)
        {
            result.page_num = 0;
            return;
        }
        result.page_num = std::clamp(result.page_num, 1, result.num_pages);
        for (int i = (result.page_num - 1) * result.results_per_page; i < result.page_num * result.results_per_page && i < result.num_results; i++)
            result.entries.emplace_back(mp_get_artist(mp_ctx.added_artists[i]));
    }
}

static void set_center_view_edit()
{
    ctx.center.type = SHOW_CENTER_EDIT;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    ctx.center.song = nullptr;
    reset_center_view_containers();
    refresh_added_songs();
}

static void set_center_view_song(LruCacheRef<Song>&& song)
{
    ctx.center.type = SHOW_CENTER_SONG;
    ctx.center.artist = mp_get_artist_from_song(song->id);
    ctx.center.playlist = nullptr;
    ctx.center.album = mp_get_album_from_song(song->id);
    ctx.center.song = std::move(song);
    reset_center_view_containers();

    ctx.center.pinned = mp_uid_pinned(mp_get_song_uid(ctx.center.song->id));
    mp_get_song_lyrics(ctx.center.song->id, ctx.center.song_lyrics, sizeof(ctx.center.song_lyrics));
    mp_get_song_lyrics(ctx.center.song->id, ctx.center.edit.song_lyrics, sizeof(ctx.center.edit.song_lyrics));
    snprintf(ctx.center.edit.song, sizeof(ctx.center.edit.song), "%s", ctx.center.song->name.c_str());
    if (ctx.center.artist != nullptr)
    {
        snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
        ctx.center.edit.artist_id = ctx.center.artist->id;
    }
    if (ctx.center.album != nullptr)
    {
        snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
        ctx.center.edit.album_id = ctx.center.album->id;
        ctx.center.edit.track = mp_get_song_track(ctx.center.song->id);
    }
    snprintf(ctx.center.edit.song_path, sizeof(ctx.center.edit.song_path), "%s", ctx.center.song->path.c_str());
    ctx.center.edit.year = ctx.center.song->year;

    ctx.center.edit.volume = ctx.center.song->volume;

    MPUID uid = mp_get_song_uid(ctx.center.song->id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
    {
        ctx.center.edit.art = mp_get_song_art(ctx.center.song->id);
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
    }
    else
    {
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
    }

    push_history_entry(SHOW_CENTER_SONG, ctx.center.song->id);
}

static void set_center_view_album(LruCacheRef<Album>&& album)
{
    ctx.center.type = SHOW_CENTER_ALBUM;
    ctx.center.song = nullptr;
    ctx.center.artist = mp_get_artist_from_album(album->id);
    ctx.center.playlist = nullptr;
    ctx.center.album = std::move(album);
    reset_center_view_containers();

    ctx.center.pinned = mp_uid_pinned(mp_get_album_uid(ctx.center.album->id));
    std::vector<SongTrackID> tracks = mp_get_songs_from_album(ctx.center.album->id);
    for (auto & [song_id, track] : tracks)
    {
        ctx.center.song_tracks.emplace_back(
                mp_get_song(song_id), 
                nullptr,
                nullptr,
                track);
    }

    snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
    if (ctx.center.artist != nullptr)
    {
        snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
        ctx.center.edit.artist_id = ctx.center.artist->id;
    }
    ctx.center.edit.year = ctx.center.album->year;

    MPUID uid = mp_get_album_uid(ctx.center.album->id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
    {
        ctx.center.edit.art = mp_get_song_art(ctx.center.album->id);
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
    }
    else
    {
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
    }

    push_history_entry(SHOW_CENTER_ALBUM, ctx.center.album->id);
}

static void set_center_view_playlist(LruCacheRef<Playlist>&& playlist)
{
    ctx.center.type = SHOW_CENTER_PLAYLIST;
    ctx.center.song = nullptr;
    ctx.center.artist = nullptr;
    ctx.center.playlist = std::move(playlist);
    ctx.center.album = nullptr;
    reset_center_view_containers();

    ctx.center.pinned = mp_uid_pinned(mp_get_playlist_uid(ctx.center.playlist->id));
    std::vector<SongTrackID> tracks = mp_get_songs_from_playlist(ctx.center.playlist->id);
    for (auto & [song_id, track] : tracks)
    {
        ctx.center.song_tracks.emplace_back(
                mp_get_song(song_id),
                mp_get_album_from_song(song_id),
                mp_get_artist_from_song(song_id),
                track);
    }

    snprintf(ctx.center.edit.playlist, sizeof(ctx.center.edit.playlist), "%s", ctx.center.playlist->name.c_str());

    MPUID uid = mp_get_playlist_uid(ctx.center.playlist->id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
    {
        ctx.center.edit.art = mp_get_playlist_art(ctx.center.playlist->id);
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
    }
    else
    {
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
    }

    push_history_entry(SHOW_CENTER_PLAYLIST, ctx.center.playlist->id);
}

static void set_center_view_artist(LruCacheRef<Artist>&& artist)
{
    ctx.center.type = SHOW_CENTER_ARTIST;
    ctx.center.song = nullptr;
    ctx.center.artist = std::move(artist);
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    reset_center_view_containers();

    ctx.center.pinned = mp_uid_pinned(mp_get_artist_uid(ctx.center.artist->id));
    std::vector<AlbumID> albums = mp_get_albums_from_artist(ctx.center.artist->id);
    for (AlbumID album_id : albums)
        ctx.center.albums.emplace_back(mp_get_album(album_id));

    ctx.center.song_search_result = mp_get_paginated_songs_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, 1);
    snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());

    MPUID uid = mp_get_artist_uid(ctx.center.artist->id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
    {
        ctx.center.edit.art = mp_get_song_art(ctx.center.artist->id);
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
    }
    else
    {
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_artist_slot);
    }

    push_history_entry(SHOW_CENTER_ARTIST, ctx.center.artist->id);
}

static void set_center_view_search_result()
{
    ctx.center.type = SHOW_CENTER_SEARCH;
    ctx.center.song = nullptr;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    reset_center_view_containers();

    if (ctx.center.search_result_type == SEARCH_TYPE_SONG)
        ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    else if (ctx.center.search_result_type == SEARCH_TYPE_ALBUM)
        ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    else if (ctx.center.search_result_type == SEARCH_TYPE_ARTIST)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    else if (ctx.center.search_result_type == SEARCH_TYPE_PLAYLIST)
        ctx.center.playlist_search_result = mp_search_playlists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
}

static void toggle_right_view_queue()
{
    ctx.queue_open = !ctx.queue_open;
}

static void initialize_default_texture(GLuint* id, const char* path)
{
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int nc;
    FILE* fptr = fopen(path, "rb");
    int width, height;
    unsigned char* data = stbi_load_from_file(fptr, &width, &height, &nc, 4);
    assert(data);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    fclose(fptr);
}

static void initialize_default_texture_slot(const char* path)
{
    int width, height, nc;
    unsigned char* data = stbi_load(path, &width, &height, &nc, 4);
    unsigned char* resized_data = stbir_resize_uint8_linear(
                data, width, height, 0,
                NULL, ART_WIDTH, ART_WIDTH, 0,
                STBIR_4CHANNEL);
    ctx.textures.default_album_slot = create_texture(resized_data);
    ctx.textures.edit_slot = create_texture(resized_data);
    stbi_image_free(data);
    free(resized_data);
}

static void initialize_default_artist_slot(const char* path)
{
    int width, height, nc;
    unsigned char* data = stbi_load(path, &width, &height, &nc, 4);
    unsigned char* resized_data = stbir_resize_uint8_linear(
                data, width, height, 0,
                NULL, ART_WIDTH, ART_WIDTH, 0,
                STBIR_4CHANNEL);
    ctx.textures.default_artist_slot = create_texture(resized_data);
    stbi_image_free(data);
    free(resized_data);
}

static void initialize_default_textures()
{
    initialize_default_texture_slot("assets/icons/No-album-art.png");
    initialize_default_artist_slot("assets/icons/default-artist.png");
    initialize_default_texture(&ctx.textures.play_button.id, "assets/icons/play.png");
    initialize_default_texture(&ctx.textures.skip_button.id, "assets/icons/skip.png");
    initialize_default_texture(&ctx.textures.pause_button.id, "assets/icons/pause.png");
    initialize_default_texture(&ctx.textures.repeat_button.id, "assets/icons/repeat.png");
    initialize_default_texture(&ctx.textures.shuffle_button.id, "assets/icons/shuffle.png");
    initialize_default_texture(&ctx.textures.autoplay_button.id, "assets/icons/autoplay.png");
    initialize_default_texture(&ctx.textures.volume.id, "assets/icons/volume-up.png");
    initialize_default_texture(&ctx.textures.mute.id, "assets/icons/mute.png");
    initialize_default_texture(&ctx.textures.show_queue_button.id, "assets/icons/show-queue.png");
    initialize_default_texture(&ctx.textures.menu_button.id, "assets/icons/menu.png");
    initialize_default_texture(&ctx.textures.pin_button.id, "assets/icons/push-pin.png");
    initialize_default_texture(&ctx.textures.home_button.id, "assets/icons/home.png");
    initialize_default_texture(&ctx.textures.pencil_button.id, "assets/icons/pencil.png");
    initialize_default_texture(&ctx.textures.settings_button.id, "assets/icons/setting.png");
    initialize_default_texture(&ctx.textures.lyrics_button.id, "assets/icons/song-lyrics.png");
    initialize_default_texture(&ctx.textures.close_button.id, "assets/icons/close.png");
    initialize_default_texture(&ctx.textures.maximize_button.id, "assets/icons/maximize.png");
    initialize_default_texture(&ctx.textures.round_rewind_button.id, "assets/icons/round-rewind.png");
    initialize_default_texture(&ctx.textures.mixer_button.id, "assets/icons/mixer.png");
    initialize_default_texture(&ctx.textures.search.id, "assets/icons/search.png");
}

void ui_init()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        exit(1);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    int window_width = (int)(1280 * main_scale);
    int window_height = (int)(800 * main_scale);

    ctx.window = glfwCreateWindow(window_width, window_height, "yamp", nullptr, nullptr);
    if (ctx.window == nullptr)
        exit(1);
    glfwMakeContextCurrent(ctx.window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);
    glfwSetKeyCallback(ctx.window, key_callback);

    initialize_default_textures();
    
    assert(pfd::settings::available() && "Portable File Dialogs are not available on this platform.\n");
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 20.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    //style.ItemSpacing = ImVec2(0.0f, 0.0f);
    style.ItemInnerSpacing = ImVec2(0.0f, 0.0f);
    style.CellPadding = ImVec2(0.0f, 0.0f);
    style.TabRounding = 0.0f;

    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    style.WindowBorderSize = 0.0f;

    ImGuiIO& io = ImGui::GetIO();

    const char* font_file_path = "assets/fonts/Noto_Sans/NotoSans-VariableFont_wdth,wght.ttf";
    io.FontDefault = io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase);
    assert(io.FontDefault != nullptr);

    {
        ImFontConfig font_config{};
        const char* font_file_path = "assets/fonts/Noto_Color_Emoji/NotoColorEmoji-Regular.ttf";
        font_config.MergeMode = true;
        font_config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
        io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config);
    }

    {
        ImFontConfig font_config{};
        const char* font_file_path = "assets/fonts/Noto_Sans_Symbols/NotoSansSymbols-VariableFont_wght.ttf";
        font_config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config);
    }

    {
        ImFontConfig font_config{};
        const char* font_file_path = "assets/fonts/Noto_Sans_JP/NotoSansJP-Regular.ttf";
        font_config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config);
    }

    {
        ImFontConfig font_config{};
        const char* font_file_path = "assets/fonts/Noto_Sans_TC/NotoSansTC-Regular.ttf";
        font_config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config);
    }

    //font_file_path = "assets/fonts/Inter,Noto_Color_Emoji/Noto_Color_Emoji/NotoColorEmoji-Regular.ttf";
    //font_config.MergeMode = true;
    //font_config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
    //io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config);

    //const ImWchar icons_ranges[] = { 0x0030, 0x0039, 0x002F, 0x002F, 0 };
    //font_file_path = "assets/fonts/mono/fonts/ttf/MartianMonoSemiExpanded-Light.ttf";
    //io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, &font_config, icons_ranges);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);

    mp_ctx.songs.set_constructor_callback(
        []([[maybe_unused]] Song* song) -> void
        {
            SPDLOG_INFO("Loading Song {} {}", song->id, song->name);
        });

    mp_ctx.albums.set_constructor_callback(
        []([[maybe_unused]] Album* album) -> void
        {
            SPDLOG_INFO("Loading Album {} {}", album->id, album->name);
        });

    mp_ctx.artists.set_constructor_callback(
        []([[maybe_unused]] Artist* artist) -> void
        {
            SPDLOG_INFO("Loading Artist {} {}", artist->id, artist->name);
        });

    mp_ctx.playlists.set_constructor_callback(
        []([[maybe_unused]] Playlist* playlist) -> void
        {
            SPDLOG_INFO("Loading Playlist {} {}", playlist->id, playlist->name);
        });

    mp_ctx.songs.set_destructor_callback(
        [](Song* song) -> void
        {
            SPDLOG_INFO("Unloading Song {} {}", song->id, song->name);
            delete_texture_with_uid(mp_get_song_uid(song->id));
        });

    mp_ctx.albums.set_destructor_callback(
        [](Album* album) -> void
        {
            SPDLOG_INFO("Unloading Album {} {}", album->id, album->name);
            delete_texture_with_uid(mp_get_album_uid(album->id));
        });

    mp_ctx.artists.set_destructor_callback(
        [](Artist* artist) -> void
        {
            SPDLOG_INFO("Unloading Artist {} {}", artist->id, artist->name);
            delete_texture_with_uid(mp_get_artist_uid(artist->id));
        });

    mp_ctx.playlists.set_destructor_callback(
        [](Playlist* playlist) -> void
        {
            SPDLOG_INFO("Unloading Playlist {} {}", playlist->id, playlist->name);
            delete_texture_with_uid(mp_get_playlist_uid(playlist->id));
        });
}

static void destroy_cache_references()
{
    // cannot destroy cache before cacheref
    ctx.center.song_tracks.clear();
    ctx.center.songs.clear();
    ctx.center.albums.clear();
    ctx.center.song.release();
    ctx.center.playlist.release();
    ctx.center.album.release();
    ctx.center.artist.release();
    ctx.center.song_search_result.entries.clear();
    ctx.center.album_search_result.entries.clear();
    ctx.center.artist_search_result.entries.clear();
    ctx.center.playlist_search_result.entries.clear();
    ctx.center.added_songs.entries.clear();
    ctx.center.added_albums.entries.clear();
    ctx.center.added_artists.entries.clear();
}

void ui_cleanup()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();

    destroy_cache_references();

    SPDLOG_INFO("UI cleaned up");
}

static void draw_add_to_playlist_popup(SongID song_id)
{
    (void)song_id;
    if (ImGui::Button("Add to Playlist"))
        ImGui::OpenPopup("add_to_playlist_popup");
    if (ImGui::BeginPopup("add_to_playlist_popup"))
    {
        if (ImGui::Button("Add To Playlist"))
        {
            //mp_add_song_to_playlist(song_id, playlist_id);
        }
        ImGui::Text("Create Playlist");
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

struct DrawSongTableParams {
    std::vector<SongAlbumArtistTrack>* song_tracks;
    std::vector<SongAlbumArtist>* songs;
    std::vector<float*> column_offsets;
    bool show_track = false;
    bool show_art = true;
    bool show_name = true;
    bool show_album = true;
    bool show_artist = true;
    bool show_length = true;
};

static void draw_song_table(DrawSongTableParams& params)
{
    LruCacheRef<Song> center_song = nullptr;
    LruCacheRef<Artist> center_artist = nullptr;
    LruCacheRef<Album> center_album = nullptr;

    CustomTableParams custom_table_params{};
    custom_table_params.num_cols = params.show_track 
                                 + params.show_name
                                 + params.show_album
                                 + params.show_artist
                                 + params.show_length;
    assert(static_cast<size_t>(custom_table_params.num_cols) == params.column_offsets.size());
    assert(!(params.show_track && params.song_tracks == nullptr));

    if (params.show_track)
        custom_table_params.num_rows = static_cast<int>(params.song_tracks->size());
    else
        custom_table_params.num_rows = static_cast<int>(params.songs->size());

    custom_table_params.header_row_height = 40.0f;
    custom_table_params.row_height = 48.0f;
    custom_table_params.table_width = ImGui::GetContentRegionAvail().x;

    static float name_col_width = 300.0f;
    static float artist_col_width = 300.0f;
    static float album_col_width = 300.0f;
    static float length_col_width = 50.0f;

    static int selected_row = -1;

    custom_table_params.selected_row = &selected_row;
    custom_table_params.menu_popup_callback = [&params](int* selected_row) -> MenuPopupEnum
        {
            LruCacheRef<Song>& song = (params.show_track)
                                ? params.song_tracks->at(*selected_row).song
                                : params.songs->at(*selected_row).song;
            if (ImGui::Button("Show"))
            {
                *selected_row = -1;
                set_center_view_song(std::move(song));
                return MENU_CLOSE_TABLE;
            }
            if (ImGui::Button("Play"))
            {
                mp_play_song(song->id);
                return MENU_CLOSE_POPUP;
            }
            if (ImGui::Button("Queue"))
            {
                mp_queue_song(song->id);
                return MENU_CLOSE_POPUP;
            }
            draw_add_to_playlist_popup(song->id);
            if (ImGui::Button("Close"))
                return MENU_CLOSE_POPUP;

            return MENU_END_POPUP;
        };

    if (params.show_track)
    {
        custom_table_params.col_widths.push_back(50.0f);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("#", region_start, region_size, ALIGN_CENTER, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                SongAlbumArtistTrack& abysmal_conglomeration = params.song_tracks->at(row); 
                LruCacheRef<Song>& song = abysmal_conglomeration.song;
                int track = abysmal_conglomeration.track;
                ImVec2 play_button_size(18.0f, 18.0f);
                ImGui::SetCursorPos(ImVec2(region_start.x + (region_size.x - play_button_size.x) / 2.0f, region_start.y + (region_size.y - play_button_size.y) / 2.0f));
                ImVec2 screen_pos = ImGui::GetCursorScreenPos();
                const bool hovered = imgui_is_region_hovered(screen_pos, play_button_size);
                if (!hovered)
                {
                    std::string track_str = std::to_string(track);
                    ImVec2 text_size = ImGui::CalcTextSize(track_str.c_str());
                    ImGui::SetCursorPos(ImVec2(region_start.x + (region_size.x - text_size.x) / 2.0f, region_start.y + (region_size.y - text_size.y) / 2.0f));
                    ImGui::Text("%s", track_str.c_str());
                }
                else
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    GLTexture tex = ctx.textures.play_button;
                    ImGui::PushStyleColor(ImGuiCol_Button, BLACK);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BLACK);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, BLACK);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                    const ImVec4 bg_col = INVISIBILE;
                    const ImVec4 tint_col = WHITE;
                    ImGui::ImageButton("Play", tex.id, play_button_size, UV0, UV1, bg_col, tint_col);
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                            mp_queue_song(song->id);
                        else
                            mp_play_song(song->id);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(3);
                }
            });
    }

    if (params.show_name)
    {
        custom_table_params.col_widths.push_back(name_col_width);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("Title", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&center_song, &params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                LruCacheRef<Song>& song = (params.show_track)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row).song;

                ImVec2 picture_size{};
                ImGui::SetCursorPos(region_start);
                float padding = 0.0f;
                if (params.show_art)
                {
                    picture_size = ImVec2{48.0f, 48.0f};
                    padding = 5.0f;
                    const bool hovered = imgui_is_region_hovered(ImGui::GetCursorScreenPos(), picture_size);
                    GLTexture tex = get_texture_from_song(song->id);
                    ImVec4 tint = (hovered) ? GRAY : WHITE;
                    ImGui::ImageWithBg(tex.id, picture_size, tex.uv0, tex.uv1, INVISIBILE, tint);
                    if (hovered)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        GLTexture tex = ctx.textures.play_button;
                        ImVec2 play_button_size(18.0f, 18.0f);
                        ImVec2 play_button_pos(
                                region_start.x + (picture_size.x - play_button_size.x) / 2.0f,
                                region_start.y + (picture_size.y - play_button_size.y) / 2.0f);
                        ImGui::SetCursorPos(play_button_pos);
                        ImGui::PushStyleColor(ImGuiCol_Button, BLACK);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BLACK);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, BLACK);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                        if (imgui_image_button(tex.id, play_button_size) || ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                                mp_queue_song(song->id);
                            else
                                mp_play_song(song->id);
                        }
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(3);
                    }
                }
                bool pressed = imgui_aligned_text_button(song->name.c_str(), ImVec2(region_start.x + picture_size.x + padding, region_start.y), ImVec2(region_size.x - picture_size.x - padding, region_size.y), ALIGN_LEFT, ALIGN_CENTER);
                if (pressed)
                    center_song = mp_get_song(song->id);
            });
    }

    if (params.show_album)
    {
        custom_table_params.col_widths.push_back(album_col_width);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("Album", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&center_album, &params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                LruCacheRef<Album>& album = (params.show_track)
                                    ? params.song_tracks->at(row).album
                                    : params.songs->at(row).album;

                if (album == nullptr)
                    return;

                if (imgui_aligned_text_button(album->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                    center_album = mp_get_album(album->id);
            });
    }

    if (params.show_artist)
    {
        custom_table_params.col_widths.push_back(artist_col_width);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("Artist", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&center_artist, &params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                LruCacheRef<Artist>& artist = (params.show_track)
                                    ? params.song_tracks->at(row).artist
                                    : params.songs->at(row).artist;


                if (artist == nullptr)
                    return;

                if (imgui_aligned_text_button(artist->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                    center_artist = mp_get_artist(artist->id);
            });
    }

    if (params.show_length)
    {
        custom_table_params.col_widths.push_back(length_col_width);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("Length", region_start, region_size, ALIGN_RIGHT, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                LruCacheRef<Song>& song = (params.show_track)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row).song;

                if (song == nullptr)
                    return;

                char length_str[256];
                int length = static_cast<int>(song->length);
                snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
                imgui_aligned_text(length_str, region_start, region_size, ALIGN_RIGHT, ALIGN_CENTER);
            });
    }

    for (size_t i = 0; i < params.column_offsets.size() - 1; i++)
        custom_table_params.col_offsets.push_back(params.column_offsets[i]);

    float* org_right_column_offset = params.column_offsets.back();
    float right_column_offset = custom_table_params.table_width - (*org_right_column_offset);
    custom_table_params.col_offsets.push_back(&right_column_offset);

    imgui_custom_table(custom_table_params);

    *org_right_column_offset = custom_table_params.table_width - right_column_offset;

    if (center_song != nullptr)
        set_center_view_song(std::move(center_song));
    if (center_artist != nullptr)
        set_center_view_artist(std::move(center_artist));
    if (center_album != nullptr)
        set_center_view_album(std::move(center_album));
}

static void draw_album_table(std::vector<LruCacheRef<Album>>& albums)
{
    LruCacheRef<Album> center_album = nullptr;
    ImGui::BeginChild("album_table", ImGui::GetContentRegionAvail());
    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += DEFAULT_MARGIN;
    region_start.y += DEFAULT_MARGIN;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = DEFAULT_MARGIN;
    for (LruCacheRef<Album>& album : albums)
    {
        ImGui::PushID(album->id);
        GLTexture tex = get_texture_from_album(album->id);
        float text_height = ImGui::CalcTextSize(album->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        if (imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1))
        {
            center_album = mp_get_album(album->id);
            ImGui::PopID();
            break;
        }
        if (imgui_aligned_text_button(album->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            center_album = mp_get_album(album->id);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    ImGui::Dummy({0.0f, spacing});
    imgui_pop_font_scale();
    ImGui::EndChild();

    if (center_album != nullptr)
        set_center_view_album(std::move(center_album));
}

static void draw_playlist_table(std::vector<LruCacheRef<Playlist>>& playlists)
{
    LruCacheRef<Playlist> center_playlist = nullptr;
    ImGui::BeginChild("playlist_table", ImGui::GetContentRegionAvail());
    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += DEFAULT_MARGIN;
    region_start.y += DEFAULT_MARGIN;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = DEFAULT_MARGIN;
    for (LruCacheRef<Playlist>& playlist : playlists)
    {
        ImGui::PushID(playlist->id);
        GLTexture tex = get_texture_from_playlist(playlist->id);
        float text_height = ImGui::CalcTextSize(playlist->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        if (imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1))
        {
            center_playlist = mp_get_playlist(playlist->id);
            ImGui::PopID();
            break;
        }
        if (imgui_aligned_text_button(playlist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            center_playlist = mp_get_playlist(playlist->id);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    ImGui::Dummy({0.0f, spacing});
    imgui_pop_font_scale();
    ImGui::EndChild();

    if (center_playlist != nullptr)
        set_center_view_playlist(std::move(center_playlist));
}

static void draw_artist_table(std::vector<LruCacheRef<Artist>>& artists)
{
    ImGui::BeginChild("artist_table", ImGui::GetContentRegionAvail());
    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += 10.0f;
    region_start.y += 10.0f;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = 10.0f;
    for (LruCacheRef<Artist>& artist : artists)
    {
        ImGui::PushID(artist->id);
        GLTexture tex = get_texture_from_artist(artist->id);
        float text_height = ImGui::CalcTextSize(artist->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
        if (imgui_aligned_text_button(artist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            set_center_view_artist(mp_get_artist(artist->id));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    ImGui::Dummy({0.0f, spacing});
    imgui_pop_font_scale();
    ImGui::EndChild();
}

static void draw_search_results()
{
    static int current_search_type = static_cast<int>(ctx.center.search_result_type);

    const float padding = 10.0f;
    const float header_height = 36.0f;

    ImGui::SetCursorPos(ImVec2(0.0f, padding + header_height));

    if (current_search_type == SEARCH_TYPE_SONG)
    {
        static float name_col_offset = 10.0f;
        static float artist_col_offset = 300.0f;
        static float album_col_offset = 550.0f;
        static float length_col_right_offset = 100.0f;

        DrawSongTableParams params{};
        params.songs = &ctx.center.song_search_result.entries;
        params.column_offsets = {&name_col_offset, &artist_col_offset, &album_col_offset, &length_col_right_offset};
        draw_song_table(params);
    }
    else if (current_search_type == SEARCH_TYPE_ALBUM)
    {
        std::vector<LruCacheRef<Album>> albums{};
        imgui_separator();
        draw_album_table(ctx.center.album_search_result.entries);
    }
    else if (current_search_type == SEARCH_TYPE_ARTIST)
    {
        std::vector<LruCacheRef<Artist>> artists{};
        imgui_separator();
        draw_artist_table(ctx.center.artist_search_result.entries);
    }
    else if (current_search_type == SEARCH_TYPE_PLAYLIST)
    {
        imgui_separator();
        draw_playlist_table(ctx.center.playlist_search_result.entries);
    }

    ImGui::SetCursorPos(ImVec2(padding, padding));

    const char* search_types[] = {
        "Songs",
        "Albums",
        "Artist",
        "Playlist"
    };

    int page_num{};
    int num_pages{};
    int num_results{};
    if (current_search_type == SEARCH_TYPE_SONG)
    {
        page_num = ctx.center.song_search_result.page_num;
        num_pages = ctx.center.song_search_result.num_pages;
        num_results = ctx.center.song_search_result.num_results;
    }
    else if (current_search_type == SEARCH_TYPE_ALBUM)
    {
        page_num = ctx.center.album_search_result.page_num;
        num_pages = ctx.center.album_search_result.num_pages;
        num_results = ctx.center.album_search_result.num_results;
    }
    else if (current_search_type == SEARCH_TYPE_ARTIST)
    {
        page_num = ctx.center.artist_search_result.page_num;
        num_pages = ctx.center.artist_search_result.num_pages;
        num_results = ctx.center.artist_search_result.num_results;
    }
    else if (current_search_type == SEARCH_TYPE_PLAYLIST)
    {
        page_num = ctx.center.playlist_search_result.page_num;
        num_pages = ctx.center.playlist_search_result.num_pages;
        num_results = ctx.center.playlist_search_result.num_results;
    }

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search_box", "Search...", ctx.search_query, sizeof(ctx.search_query))) 
    {
        if (current_search_type == SEARCH_TYPE_SONG)
            ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_ALBUM)
            ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_ARTIST)
            ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_PLAYLIST)
            ctx.center.playlist_search_result = mp_search_playlists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    }
    if (ImGui::IsItemClicked())
        snprintf(ctx.search_query, sizeof(ctx.search_query), "");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##search_combo", &current_search_type, search_types, std::size(search_types)))
    {
        ctx.center.search_result_type = static_cast<SearchResultEnum>(current_search_type);
        if (current_search_type == SEARCH_TYPE_SONG)
            ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_ALBUM)
            ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_ARTIST)
            ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        else if (current_search_type == SEARCH_TYPE_PLAYLIST)
            ctx.center.playlist_search_result = mp_search_playlists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
    {
        if (current_search_type == SEARCH_TYPE_SONG)
            ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.song_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_ALBUM)
            ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.album_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_ARTIST)
            ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.artist_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_PLAYLIST)
            ctx.center.playlist_search_result = mp_search_playlists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.playlist_search_result.page_num);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num < num_pages)
    {
        if (current_search_type == SEARCH_TYPE_SONG)
            ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.song_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_ALBUM)
            ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.album_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_ARTIST)
            ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.artist_search_result.page_num);
        else if (current_search_type == SEARCH_TYPE_PLAYLIST)
            ctx.center.playlist_search_result = mp_search_playlists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.playlist_search_result.page_num);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);
}

static void draw_song_info_edit_album_picker()
{
    const float padding = 10.0f;
    int page_num = ctx.center.album_search_result.page_num;
    int num_pages = ctx.center.album_search_result.num_pages;
    int num_results = ctx.center.album_search_result.num_results;

    ImGui::SetCursorPos(ImVec2(padding, padding));
    if (ImGui::Button("Close"))
        ctx.center.show_split = SHOW_NONE;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search_box", "Search...", ctx.search_query, sizeof(ctx.search_query)))
        ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 0.0f));

    ImGui::SameLine();
    if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
        ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.album_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num > num_pages)
        ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.album_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
    ImGui::Separator();

    ImGui::PopStyleVar();

    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += 10.0f;
    region_start.y += 10.0f;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = 10.0f;
    for (LruCacheRef<Album>& test_album : ctx.center.album_search_result.entries)
    {
        ImGui::PushID(test_album->id);
        GLTexture tex = get_texture_from_album(test_album->id);
        float text_height = ImGui::CalcTextSize(test_album->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
        if (imgui_aligned_text_button(test_album->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            ctx.center.edit.changed_info = true;
            ctx.center.edit.album_id = test_album->id;
            snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", test_album->name.c_str());
            LruCacheRef<Artist> artist = mp_get_artist_from_album(test_album->id);
            ctx.center.edit.artist_id = artist->id;
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", artist->name.c_str());
            ctx.center.show_split = SHOW_NONE;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    imgui_pop_font_scale();
}

static void draw_song_info_edit_artist_picker()
{
    const float padding = 10.0f;
    int page_num = ctx.center.artist_search_result.page_num;
    int num_pages = ctx.center.artist_search_result.num_pages;
    int num_results = ctx.center.artist_search_result.num_results;

    ImGui::SetCursorPos(ImVec2(padding, padding));
    if (ImGui::Button("Close"))
        ctx.center.show_split = SHOW_NONE;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search_box", "Search...", ctx.search_query, sizeof(ctx.search_query)))
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 0.0f));

    ImGui::SameLine();
    if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num > num_pages)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
    ImGui::Separator();

    ImGui::PopStyleVar();

    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += 10.0f;
    region_start.y += 10.0f;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = 10.0f;
    for (LruCacheRef<Artist>& test_artist : ctx.center.artist_search_result.entries)
    {
        ImGui::PushID(test_artist->id);
        GLTexture tex = get_texture_from_artist(test_artist->id);
        float text_height = ImGui::CalcTextSize(test_artist->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
        if (imgui_aligned_text_button(test_artist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            ctx.center.edit.changed_info = true;
            ctx.center.edit.artist_id = test_artist->id;
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", test_artist->name.c_str());
            ctx.center.show_split = SHOW_NONE;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    imgui_pop_font_scale();
}

static void draw_song_info_edit_lyrics()
{
    ImGui::BeginChild("lyrics", ImGui::GetContentRegionAvail());
    imgui_push_font_scale(1.2f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK_GRAY);
    ctx.center.edit.changed_info |= ImGui::InputTextMultiline(
            "##textarea",
            ctx.center.edit.song_lyrics,
            sizeof(ctx.center.edit.song_lyrics),
            ImVec2(-1.0f, -1.0f)
        );
    ImGui::PopStyleColor();
    imgui_pop_font_scale();
    ImGui::EndChild();
}

static void draw_song_info_edit()
{
    static bool set_active;
    const bool split = ctx.center.show_split != SHOW_NONE;

    ImGui::BeginTable("table", 1 + split, ImGuiTableFlags_Resizable);
    ImGui::TableNextColumn();

    ImGui::BeginChild("child1", ImGui::GetContentRegionAvail());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK_GRAY);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    float col_1_cursor_x = 10.0f;
    float col_2_cursor_x = 120.0f;
    imgui_push_font_scale(1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Song", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.song[0] = '\0';
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool song_name_empty = ctx.center.edit.song[0] == '\0';
    if (song_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (ctx.center.edit.edit_type == EDIT_SONG_NAME)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputText("##Song", ctx.center.edit.song, sizeof(ctx.center.edit.song));
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((song_name_empty) ? EMPTY_STR : ctx.center.edit.song, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_SONG_NAME;
        set_active = true;
    }
    if (song_name_empty)
        ImGui::PopStyleColor();

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Album", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.album[0] = '\0';
        ctx.center.edit.album_id = INVALID_ID;
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool album_name_empty = ctx.center.edit.album[0] == '\0';
    if (album_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (imgui_aligned_text_button((album_name_empty) ? EMPTY_STR : ctx.center.edit.album, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.show_split = SHOW_ALBUM_PICKER;
        ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    }
    if (album_name_empty)
        ImGui::PopStyleColor();

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Track", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.track = INVALID_TRACK;
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool invalid_track = (ctx.center.edit.track == INVALID_TRACK || ctx.center.edit.album_id == INVALID_ID) && ctx.center.edit.edit_type != EDIT_SONG_TRACK;
    if (invalid_track)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (ctx.center.edit.edit_type == EDIT_SONG_TRACK)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputInt("##track", &ctx.center.edit.track, 0);
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((invalid_track) ? EMPTY_STR : std::to_string(ctx.center.edit.track).c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_SONG_TRACK;
        set_active = true;
    }
    if (invalid_track)
        ImGui::PopStyleColor();

    if (ctx.center.edit.album_id == INVALID_ID)
    {
        ImGui::SetCursorPosX(col_1_cursor_x);
        if (imgui_aligned_text_button("Artist", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
        {
            ctx.center.edit.artist[0] = '\0';
            ctx.center.edit.artist_id = INVALID_ID;
            ctx.center.edit.changed_info = true;
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        const bool artist_name_empty = ctx.center.edit.artist[0] == '\0';
        if (artist_name_empty)
            ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
        if (imgui_aligned_text_button((artist_name_empty) ? EMPTY_STR : ctx.center.edit.artist, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
        {
            if (ctx.center.edit.album_id == INVALID_ID)
            {
                ctx.center.show_split = SHOW_ARTIST_PICKER;
                ctx.search_query[0] = '\0';
                ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
            }
        }
        if (artist_name_empty)
            ImGui::PopStyleColor();
    }
    else
    {
        ImGui::SetCursorPosX(col_1_cursor_x);
        imgui_aligned_text("Artist", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        imgui_aligned_text(ctx.center.edit.artist, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Year", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.changed_info = ctx.center.edit.year != INVALID_YEAR;
        ctx.center.edit.year = INVALID_YEAR;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool invalid_year = ctx.center.edit.year == INVALID_YEAR && ctx.center.edit.edit_type != EDIT_SONG_YEAR;
    if (invalid_year)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
    if (ctx.center.edit.edit_type == EDIT_SONG_YEAR)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputInt("##Year", &ctx.center.edit.year, 0);
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((invalid_year) ? EMPTY_STR : std::to_string(ctx.center.edit.year).c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_SONG_YEAR;
        set_active = true;
    }
    if (invalid_year)
        ImGui::PopStyleColor();

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Lyrics", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.changed_info |= ctx.center.edit.song_lyrics[0] != '\0';
        ctx.center.edit.song_lyrics[0] = '\0';
        ctx.center.show_split = SHOW_EDIT_LYRICS;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    if (imgui_aligned_text_button((ctx.center.show_split == SHOW_EDIT_LYRICS) ? "Close" : "Open", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        if (ctx.center.show_split != SHOW_EDIT_LYRICS)
            ctx.center.show_split = SHOW_EDIT_LYRICS;
        else
            ctx.center.show_split = SHOW_NONE;
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Path", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.song_path[0] = '\0';
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool song_path_empty = ctx.center.edit.song_path[0] == '\0';
    if (song_path_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (imgui_aligned_text_button((song_path_empty) ? EMPTY_STR : ctx.center.edit.song_path, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string
            {
                const std::string name{"Choose mp3"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"MP3 File", "*.mp3"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> db_path = pfd::open_file(name, default_path, filters, options).result();
                return (db_path.size() > 0) ? std::move(db_path.front()) : std::string{};
            });

        ctx.prompt_callback = []()
            {
                std::string db_path = ctx.prompt->get();
                if (db_path.size() > 0)
                {
                    snprintf(ctx.center.edit.song_path, sizeof(ctx.center.edit.song_path), "%s", db_path.c_str());
                    ctx.center.edit.changed_info = true;
                    ctx.center.edit.changed_art = true;
                }
            };
    }
    if (song_path_empty)
        ImGui::PopStyleColor();

    GLTexture tex = ctx.textures.volume;
    ImVec2 volume_slider_size = ImVec2(150.0f, 5.0f);
    ImVec2 volume_icon_size = ImVec2(20.0f, 20.0f);
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Volume", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.song->volume = 1.0f;
        if (mp_ctx.current_song.song != nullptr && ctx.center.song->id == mp_ctx.current_song.song->id)
            mp_update_volume(ctx.center.song->volume);
        ctx.center.edit.changed_info = true;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    ImGui::Image(tex.id, volume_icon_size);
    ImGui::PopStyleVar(2);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (volume_icon_size.y - volume_slider_size.y) / 2.0f);
    if (imgui_custom_slider_float("##Volume", &ctx.center.song->volume, 0.0f, 2.0f, volume_slider_size))
    {
        if (mp_ctx.current_song.song != nullptr && ctx.center.song->id == mp_ctx.current_song.song->id)
            mp_update_volume(ctx.center.song->volume);
        ctx.center.edit.changed_info = true;
    }

    GLTexture edit_tex = get_texture_from_slot(ctx.textures.edit_slot);
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Cover", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.art.clear();
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        ctx.center.edit.changed_info = true;
        ctx.center.edit.changed_art = true;
        ctx.center.edit.reset_art = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    if (imgui_image_button(edit_tex.id, LARGE_IMAGE_SIZE, WHITE, edit_tex.uv0, edit_tex.uv1) && !ctx.prompt.has_value())
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string name{"Choose file"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"JPG", "*.jpg"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> path = pfd::open_file(name, default_path, filters, options).result();
                if (path.size() > 0)
                {
                    int width, height, nc;
                    uint8_t* raw_art_data = stbi_load(path.front().c_str(), &width, &height, &nc, 4);
                    ctx.center.edit.art.clear();
                    ctx.center.edit.art.data = stbir_resize_uint8_linear(
                                raw_art_data, width, height, 0,
                                NULL, ART_WIDTH, ART_WIDTH, 0,
                                STBIR_4CHANNEL
                            );
                    ctx.center.edit.art.size = ART_WIDTH * ART_WIDTH * 4 * sizeof(uint8_t);
                    stbi_image_free(raw_art_data);
                    return std::move(path.front());
                }
                return std::string{};
            });
        ctx.prompt_callback = 
            []()
            {
                std::string path = ctx.prompt->get();
                if (path.size() > 0)
                {
                    update_texture_slot(ctx.textures.edit_slot, ctx.center.edit.art.data);
                    ctx.center.edit.changed_info = true;
                    ctx.center.edit.changed_art = true;
                    ctx.center.edit.reset_art = false;
                }
                else
                {
                    SPDLOG_INFO("Choose album art cancelled");
                }
            };
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save"))
    {
        Song song{};
        song.id = ctx.center.song->id;
        song.name = ctx.center.edit.song;
        song.year = ctx.center.edit.year;
        song.volume = ctx.center.song->volume;
        song.path = ctx.center.edit.song_path;
        mp_update_song(song);
        if (ctx.center.edit.changed_art)
        {
            mp_update_song_art(ctx.center.song->id, ctx.center.edit.art);
            MPUID uid = mp_get_song_uid(ctx.center.song->id);
            if (ctx.center.edit.reset_art)
            {
                if (mpuid_has_valid_texture(uid))
                    delete_texture(ctx.textures.slot_map[uid]);
                ctx.textures.slot_map[uid] = ctx.textures.default_album_slot;
            }
            else
            {
                if (mpuid_has_valid_texture(uid))
                    copy_texture_slot(ctx.textures.slot_map[uid], ctx.textures.edit_slot);
                else
                    ctx.textures.slot_map[uid] = create_texture(ctx.center.edit.art.data);
            }
        }
        mp_update_song_album(ctx.center.song->id, ctx.center.edit.album_id, ctx.center.edit.track);
        mp_update_song_artist(ctx.center.song->id, ctx.center.edit.artist_id);
        mp_update_song_lyrics(ctx.center.song->id, ctx.center.edit.song_lyrics);
        snprintf(ctx.center.song_lyrics, sizeof(ctx.center.song_lyrics), "%s", ctx.center.edit.song_lyrics);

        if (ctx.center.edit.album_id == INVALID_ID)
            ctx.center.album = nullptr;
        else
            ctx.center.album = mp_get_album(ctx.center.edit.album_id);

        if (ctx.center.edit.artist_id == INVALID_ID)
            ctx.center.artist = nullptr;
        else
            ctx.center.artist = mp_get_artist(ctx.center.edit.artist_id);

        ctx.center.edit.volume = song.volume;
        ctx.center.edit.changed_info = false;
        ctx.center.edit.changed_art = false;
        ctx.center.edit.reset_art = false;
    }
    if (ctx.center.edit.changed_info)
    {
        ImGui::SameLine();
        ImGui::Text(" [Unsaved Changes]");
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Reset"))
    {
        snprintf(ctx.center.edit.song, sizeof(ctx.center.edit.song), "%s", ctx.center.song->name.c_str());
        if (ctx.center.artist != nullptr)
        {
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
            ctx.center.edit.artist_id = ctx.center.artist->id;
        }
        if (ctx.center.album != nullptr)
        {
            snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
            ctx.center.edit.album_id = ctx.center.album->id;
        }
        snprintf(ctx.center.edit.song_path, sizeof(ctx.center.edit.song_path), "%s", ctx.center.song->path.c_str());
        ctx.center.edit.year = ctx.center.song->year;

        MPUID uid = mp_get_song_uid(ctx.center.song->id);
        if (mpuid_has_valid_texture(uid))
        {
            ctx.center.edit.art = mp_get_song_art(ctx.center.song->id);
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
        }
        else
        {
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        }
        ctx.center.song->volume = ctx.center.edit.volume;
        if (mp_ctx.current_song.song != nullptr && ctx.center.song->id == mp_ctx.current_song.song->id)
            mp_update_volume(ctx.center.song->volume);

        mp_get_song_lyrics(ctx.center.song->id, ctx.center.edit.song_lyrics, sizeof(ctx.center.edit.song_lyrics));
        ctx.center.edit.changed_info = false;
        ctx.center.edit.changed_art = false;
        ctx.center.edit.reset_art = false;
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Delete"))
    {
        SongID song_id = ctx.center.song->id;
        set_center_view_home();
        mp_delete_song(song_id);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        imgui_pop_font_scale();
        ImGui::EndChild();
        ImGui::EndTable();
        return;
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Update Metadata"))
        ;

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    imgui_pop_font_scale();
    ImGui::EndChild();
    if (split)
    {
        ImGui::TableNextColumn();
        ImGui::BeginChild("child2", ImGui::GetContentRegionAvail());
        if (ctx.center.show_split == SHOW_ALBUM_PICKER)
            draw_song_info_edit_album_picker();
        else if (ctx.center.show_split == SHOW_ARTIST_PICKER)
            draw_song_info_edit_artist_picker();
        else if (ctx.center.show_split == SHOW_EDIT_LYRICS)
            draw_song_info_edit_lyrics();
        ImGui::EndChild();
    }
    ImGui::EndTable();
}

static void draw_song_info_lyrics()
{
    const ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
    imgui_push_font_scale(1.2f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + frame_padding.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + frame_padding.y);
    if (ctx.center.song_lyrics[0] == '\0')
        ImGui::Text("No Lyrics");
    else
        ImGui::TextWrapped("%s", ctx.center.song_lyrics);
    imgui_pop_font_scale();
}

static void draw_song_info()
{
    assert(ctx.center.song != nullptr);

    LruCacheRef<Song>& song = ctx.center.song;
    LruCacheRef<Artist>& artist = ctx.center.artist;
    LruCacheRef<Album>& album = ctx.center.album;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);
    GLTexture tex = get_texture_from_song(song->id);
    ImGui::ImageWithBg(tex.id, LARGE_IMAGE_SIZE, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();
    {
        ImVec2 region_start(
                origin.x + album_art_size.x + 2 * album_art_padding,
                origin.y + album_art_padding);
        ImGui::SetCursorPos(region_start);
        ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);
        ImGui::BeginChild("song_view", region_size);
        region_start = ImGui::GetCursorPos();

        imgui_push_font_scale(2.0f);
        imgui_aligned_text(song->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        imgui_pop_font_scale();

        imgui_push_font_scale(1.5f);
        if (artist) {
            if (imgui_aligned_text_button(artist->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
                set_center_view_artist(std::move(artist));
                imgui_pop_font_scale();
                ImGui::EndChild();
                return;
            }
        } else {
            //imgui_aligned_text("<No Artist>", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        }
        if (album) {
            if (imgui_aligned_text_button(album->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
                set_center_view_album(std::move(album));
                imgui_pop_font_scale();
                ImGui::EndChild();
                return;
            }
        } else {
            //imgui_aligned_text("<No Album>", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        }
        int length = static_cast<int>(ctx.center.song->length);
        if (song->year != INVALID_YEAR)
            ImGui::Text("%u • %d:%02d", song->year, length / 60, length % 60);
        else
            ImGui::Text("%d:%02d", length / 60, length % 60);

        imgui_pop_font_scale();

        ImVec2 button_size(32.0f, 32.0f);
        ImGui::SetCursorPosY(region_start.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);

        GLTexture tex = ctx.textures.play_button;
        if (imgui_image_button(tex.id, button_size))
            mp_play_song(song->id);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
        ImGui::SameLine();
        tex = ctx.textures.show_queue_button;
        if (imgui_image_button(tex.id, button_size))
            mp_queue_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pin_button.id, button_size, (ctx.center.pinned) ? RED : WHITE))
            ctx.center.pinned = mp_toggle_pin_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.lyrics_button.id, button_size, (ctx.center.show == SHOW_LYRICS) ? RED : WHITE))
        {
            if (ctx.center.show == SHOW_LYRICS)
                ctx.center.show = SHOW_NONE;
            else
                ctx.center.show = SHOW_LYRICS;
        }

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pencil_button.id, button_size, (ctx.center.show == SHOW_EDIT) ? RED : WHITE))
        {
            if (ctx.center.show == SHOW_EDIT)
                ctx.center.show = SHOW_NONE;
            else
                ctx.center.show = SHOW_EDIT;
        }

        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    imgui_separator();
    ImGui::BeginChild("more_song_info", ImGui::GetContentRegionAvail());
    if (ctx.center.show == SHOW_LYRICS)
        draw_song_info_lyrics();
    else if (ctx.center.show == SHOW_EDIT)
        draw_song_info_edit();
    ImGui::EndChild();
}

static void draw_album_info_edit_artist_picker()
{
    const float padding = 10.0f;
    int page_num = ctx.center.artist_search_result.page_num;
    int num_pages = ctx.center.artist_search_result.num_pages;
    int num_results = ctx.center.artist_search_result.num_results;

    ImGui::SetCursorPos(ImVec2(padding, padding));
    if (ImGui::Button("Close"))
        ctx.center.show_split = SHOW_NONE;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search_box", "Search...", ctx.search_query, sizeof(ctx.search_query)))
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 0.0f));

    ImGui::SameLine();
    if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num > num_pages)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
    ImGui::Separator();

    ImGui::PopStyleVar();

    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += 10.0f;
    region_start.y += 10.0f;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = 10.0f;
    for (LruCacheRef<Artist>& test_artist : ctx.center.artist_search_result.entries)
    {
        ImGui::PushID(test_artist->id);
        GLTexture tex = get_texture_from_artist(test_artist->id);
        float text_height = ImGui::CalcTextSize(test_artist->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
        if (imgui_aligned_text_button(test_artist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            ctx.center.edit.changed_info = true;
            ctx.center.edit.artist_id = test_artist->id;
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", test_artist->name.c_str());
            ctx.center.show_split = SHOW_NONE;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    imgui_pop_font_scale();
}

static void draw_album_info_edit()
{
    static bool set_active;
    const bool split = ctx.center.show_split != SHOW_NONE;

    ImGui::BeginTable("table", 1 + split, ImGuiTableFlags_Resizable);
    ImGui::TableNextColumn();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK_GRAY);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    float col_1_cursor_x = 10.0f;
    float col_2_cursor_x = 120.0f;
    imgui_push_font_scale(1.5f); 
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Album", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.album[0] = '\0';
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    ImGui::PushItemWidth(300.0f);
    const bool album_name_empty = ctx.center.edit.album[0] == '\0';
    if (album_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (ctx.center.edit.edit_type == EDIT_ALBUM_NAME)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputText("##Album", ctx.center.edit.album, sizeof(ctx.center.edit.album));
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((album_name_empty) ? EMPTY_STR : ctx.center.edit.album, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_ALBUM_NAME;
        set_active = true;
    }
    if (album_name_empty)
        ImGui::PopStyleColor();

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Year", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.changed_info = ctx.center.edit.year != INVALID_YEAR;
        ctx.center.edit.year = INVALID_YEAR;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
    const bool invalid_year = ctx.center.edit.year == INVALID_YEAR && ctx.center.edit.edit_type != EDIT_ALBUM_YEAR;
    if (invalid_year)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
    if (ctx.center.edit.edit_type == EDIT_ALBUM_YEAR)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputInt("##Year", &ctx.center.edit.year, 0);
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((invalid_year) ? EMPTY_STR : std::to_string(ctx.center.edit.year).c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_ALBUM_YEAR;
        set_active = true;
    }
    if (invalid_year)
        ImGui::PopStyleColor();

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Artist", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.artist[0] = '\0';
        ctx.center.edit.artist_id = INVALID_ID;
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool artist_name_empty = ctx.center.edit.artist[0] == '\0';
    if (artist_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (imgui_aligned_text_button((artist_name_empty) ? EMPTY_STR : ctx.center.edit.artist, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        if (ctx.center.edit.album_id == INVALID_ID)
        {
            ctx.center.show_split = SHOW_ARTIST_PICKER;
            ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        }
    }
    if (artist_name_empty)
        ImGui::PopStyleColor();

    GLTexture edit_tex = get_texture_from_slot(ctx.textures.edit_slot);
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Cover", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.art.clear();
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        ctx.center.edit.changed_info = true;
        ctx.center.edit.changed_art = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    if (imgui_image_button(edit_tex.id, LARGE_IMAGE_SIZE, WHITE, edit_tex.uv0, edit_tex.uv1) && !ctx.prompt.has_value())
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string name{"Choose file"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"JPG", "*.jpg"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> path = pfd::open_file(name, default_path, filters, options).result();
                if (path.size() > 0)
                {
                    int width, height, nc;
                    uint8_t* raw_art_data = stbi_load(path.front().c_str(), &width, &height, &nc, 4);
                    ctx.center.edit.art.clear();
                    ctx.center.edit.art.data = stbir_resize_uint8_linear(
                                raw_art_data, width, height, 0,
                                NULL, ART_WIDTH, ART_WIDTH, 0,
                                STBIR_4CHANNEL
                            );
                    ctx.center.edit.art.size = ART_WIDTH * ART_WIDTH * 4 * sizeof(uint8_t);
                    stbi_image_free(raw_art_data);
                    return std::move(path.front());
                }
                return std::string{};
            });
        ctx.prompt_callback = 
            []()
            {
                std::string path = ctx.prompt->get();
                if (path.size() > 0)
                {
                    update_texture_slot(ctx.textures.edit_slot, ctx.center.edit.art.data);
                    ctx.center.edit.changed_info = true;
                    ctx.center.edit.changed_art = true;
                }
                else
                {
                    SPDLOG_INFO("Choose album art cancelled");
                }
            };
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save"))
    {
        Album album{};
        album.id = ctx.center.album->id;
        album.name = ctx.center.edit.album;
        album.year = ctx.center.edit.year;
        mp_update_album(album);
        if (ctx.center.edit.changed_art)
        {
            mp_update_album_art(ctx.center.album->id, ctx.center.edit.art);
            MPUID uid = mp_get_album_uid(ctx.center.album->id);
            if (ctx.center.edit.reset_art)
            {
                if (mpuid_has_valid_texture(uid))
                    delete_texture(ctx.textures.slot_map[uid]);
                ctx.textures.slot_map[uid] = ctx.textures.default_album_slot;
            }
            else
            {
                if (mpuid_has_valid_texture(uid))
                    copy_texture_slot(ctx.textures.slot_map[uid], ctx.textures.edit_slot);
                else
                    ctx.textures.slot_map[uid] = create_texture(ctx.center.edit.art.data);
            }
        }
        mp_update_album_artist(ctx.center.album->id, ctx.center.edit.artist_id);

        if (ctx.center.edit.artist_id == INVALID_ID)
            ctx.center.artist = nullptr;
        else
            ctx.center.artist = mp_get_artist(ctx.center.edit.artist_id);

        ctx.center.edit.changed_info = false;
    }
    if (ctx.center.edit.changed_info)
    {
        ImGui::SameLine();
        ImGui::Text(" [Unsaved Changes]");
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Reset"))
    {
        snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
        if (ctx.center.artist != nullptr)
        {
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
            ctx.center.edit.artist_id = ctx.center.artist->id;
        }
        ctx.center.edit.year = ctx.center.album->year;

        MPUID uid = mp_get_album_uid(ctx.center.album->id);
        if (mpuid_has_valid_texture(uid))
        {
            ctx.center.edit.art = mp_get_album_art(ctx.center.album->id);
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
        }
        else
        {
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        }

        if (ctx.center.artist != nullptr)
            snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());

        ctx.center.edit.changed_info = false;
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save Year to Songs"))
    {
        mp_update_album_songs_year(ctx.center.album->id, ctx.center.album->year);
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save Cover to Songs"))
    {
        mp_update_album_songs_art(ctx.center.album->id);
        std::vector<SongTrackID> song_tracks = mp_get_songs_from_album(ctx.center.album->id);
        Slot album_art_slot = get_slot_from_album(ctx.center.album->id); 
        for (auto [song_id, track] : song_tracks)
        {
            MPUID song_uid = mp_get_song_uid(song_id);
            if (ctx.textures.slot_map.find(song_uid) != ctx.textures.slot_map.end())
                copy_texture_slot(ctx.textures.slot_map[song_uid], album_art_slot);
        }
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Delete"))
    {
        AlbumID album_id = ctx.center.album->id;
        set_center_view_home();
        mp_delete_album(album_id);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        imgui_pop_font_scale();
        ImGui::EndTable();
        return;
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    imgui_pop_font_scale(); 
    if (split)
    {
        ImGui::TableNextColumn();
        ImGui::BeginChild("child2", ImGui::GetContentRegionAvail());
        if (ctx.center.show_split == SHOW_ARTIST_PICKER)
            draw_album_info_edit_artist_picker();
        ImGui::EndChild();
    }
    ImGui::EndTable();
}

static void draw_album_info_tracklist()
{
    static float track_col_offset = 10.0f;
    static float name_col_offset = 70.0f;
    static float length_col_right_offset = 100.0f;

    DrawSongTableParams params{};
    params.show_track = true;
    params.show_album = false;
    params.show_artist = false;
    params.show_art = false;
    params.column_offsets = {&track_col_offset, &name_col_offset, &length_col_right_offset};
    params.song_tracks = &ctx.center.song_tracks;

    draw_song_table(params);
}

static void draw_album_info()
{
    assert(ctx.center.album != nullptr);

    std::vector<SongAlbumArtistTrack>& tracks = ctx.center.song_tracks;

    LruCacheRef<Album>& album = ctx.center.album;
    LruCacheRef<Artist>& artist = ctx.center.artist;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);
    
    GLTexture tex = get_texture_from_album(ctx.center.album->id);
    ImGui::ImageWithBg(tex.id, album_art_size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();
    ImVec2 region_start(
            origin.x + album_art_size.x + 2 * album_art_padding,
            origin.y + album_art_padding);
    ImGui::SetCursorPos(region_start);
    ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

    ImGui::BeginChild("album_view", region_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    region_start = ImGui::GetCursorPos();

    imgui_push_font_scale(2.0f); 
    imgui_aligned_text(album->name.c_str(), region_start, region_size);
    imgui_pop_font_scale(); 

    imgui_push_font_scale(1.5f); 
    if (artist) {
        if (imgui_aligned_text_button(artist->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
            set_center_view_artist(std::move(artist));
            imgui_pop_font_scale(); 
            ImGui::EndChild();
            return;
        }
    } else {
        ImGui::Text("<No Artist>");
    }
    int length = static_cast<int>(album->length);
    if (album->year != INVALID_YEAR)
        ImGui::Text("%u • %d:%02d", album->year, length / 60, length % 60);
    else
        ImGui::Text("%d:%02d", length / 60, length % 60);

    imgui_pop_font_scale(); 

    ImVec2 button_size(32.0f, 32.0f);
    ImGui::SetCursorPosY(region_start.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);
    if (imgui_image_button(ctx.textures.play_button.id, button_size))
    {
        mp_ctx.shuffle = false;
        mp_play_album(album->id);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.shuffle_button.id, button_size))
    {
        mp_ctx.shuffle = true;
        mp_play_album(album->id);
    }

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.show_queue_button.id, button_size))
        for (const SongAlbumArtistTrack& song : tracks)
            mp_queue_song(song.song->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pin_button.id, button_size, (ctx.center.pinned) ? RED : WHITE))
        ctx.center.pinned = mp_toggle_pin_album(album->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size, (ctx.center.show == SHOW_EDIT) ? RED : WHITE))
    {
        if (ctx.center.show == SHOW_EDIT)
            ctx.center.show = SHOW_NONE;
        else
            ctx.center.show = SHOW_EDIT;
    }

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.menu_button.id, button_size))
        ImGui::OpenPopup("add_to_playlist_popup");
    if (ImGui::BeginPopup("add_to_playlist_popup"))
    {
        ImGui::Text("Add To Playlist");
        ImGui::Text("Create Playlist");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    imgui_separator();
    ImGui::BeginChild("more_album_info", ImGui::GetContentRegionAvail());
    if (ctx.center.show == SHOW_EDIT)
        draw_album_info_edit();
    else
        draw_album_info_tracklist();
    ImGui::EndChild();
}

static void draw_playlist_info_edit()
{
    static bool set_active;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK_GRAY);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    float col_1_cursor_x = 10.0f;
    float col_2_cursor_x = 120.0f;
    imgui_push_font_scale(1.5f); 
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Name", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.playlist[0] = '\0';
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool playlist_name_empty = ctx.center.edit.playlist[0] == '\0';
    if (playlist_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (ctx.center.edit.edit_type == EDIT_PLAYLIST_NAME)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputText("##Playlist", ctx.center.edit.playlist, sizeof(ctx.center.edit.playlist));
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((playlist_name_empty) ? EMPTY_STR : ctx.center.edit.playlist, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_PLAYLIST_NAME;
        set_active = true;
    }
    if (playlist_name_empty)
        ImGui::PopStyleColor();

    GLTexture edit_tex = get_texture_from_slot(ctx.textures.edit_slot);
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Cover", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.art.clear();
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        ctx.center.edit.changed_info = true;
        ctx.center.edit.changed_art = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    if (imgui_image_button(edit_tex.id, LARGE_IMAGE_SIZE, WHITE, edit_tex.uv0, edit_tex.uv1) && !ctx.prompt.has_value())
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string name{"Choose file"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"JPG", "*.jpg"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> path = pfd::open_file(name, default_path, filters, options).result();
                if (path.size() > 0)
                {
                    int width, height, nc;
                    uint8_t* raw_art_data = stbi_load(path.front().c_str(), &width, &height, &nc, 4);
                    ctx.center.edit.art.clear();
                    ctx.center.edit.art.data = stbir_resize_uint8_linear(
                                raw_art_data, width, height, 0,
                                NULL, ART_WIDTH, ART_WIDTH, 0,
                                STBIR_4CHANNEL
                            );
                    ctx.center.edit.art.size = ART_WIDTH * ART_WIDTH * 4 * sizeof(uint8_t);
                    stbi_image_free(raw_art_data);
                    return std::move(path.front());
                }
                return std::string{};
            });
        ctx.prompt_callback = 
            []()
            {
                std::string path = ctx.prompt->get();
                if (path.size() > 0)
                {
                    update_texture_slot(ctx.textures.edit_slot, ctx.center.edit.art.data);
                    ctx.center.edit.changed_info = true;
                    ctx.center.edit.changed_art = true;
                }
                else
                {
                    SPDLOG_INFO("Choose album art cancelled");
                }
            };
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save"))
    {
        Playlist playlist{};
        playlist.id = ctx.center.playlist->id;
        playlist.name = ctx.center.edit.playlist;
        mp_update_playlist(playlist);
        if (ctx.center.edit.changed_art)
        {
            mp_update_playlist_art(ctx.center.playlist->id, ctx.center.edit.art);
            MPUID uid = mp_get_playlist_uid(ctx.center.playlist->id);
            if (ctx.center.edit.reset_art)
            {
                if (mpuid_has_valid_texture(uid))
                    delete_texture(ctx.textures.slot_map[uid]);
                ctx.textures.slot_map[uid] = ctx.textures.default_album_slot;
            }
            else
            {
                if (mpuid_has_valid_texture(uid))
                    copy_texture_slot(ctx.textures.slot_map[uid], ctx.textures.edit_slot);
                else
                    ctx.textures.slot_map[uid] = create_texture(ctx.center.edit.art.data);
            }
            ctx.center.edit.changed_art = false;
        }
        ctx.center.edit.changed_info = false;
    }
    if (ctx.center.edit.changed_info)
    {
        ImGui::SameLine();
        ImGui::Text(" [Unsaved Changes]");
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Reset"))
    {
        snprintf(ctx.center.edit.playlist, sizeof(ctx.center.edit.playlist), "%s", ctx.center.playlist->name.c_str());

        MPUID uid = mp_get_playlist_uid(ctx.center.playlist->id);
        if (mpuid_has_valid_texture(uid))
        {
            ctx.center.edit.art = mp_get_playlist_art(ctx.center.playlist->id);
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
        }
        else
        {
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_album_slot);
        }
    }
    ImGui::PopStyleVar(2);
    imgui_pop_font_scale(); 
    ImGui::PopStyleColor();
}

static void draw_playlist_info_tracklist()
{
    static float track_col_offset = 10.0f;
    static float name_col_offset = 70.0f;
    static float artist_col_offset = 300.0f;
    static float album_col_offset = 550.0f;
    static float length_col_right_offset = 100.0f;

    DrawSongTableParams params{};
    params.song_tracks = &ctx.center.song_tracks;
    params.show_track = true;
    params.column_offsets = {
        &track_col_offset,
        &name_col_offset,
        &artist_col_offset,
        &album_col_offset,
        &length_col_right_offset,
    };
    draw_song_table(params);
}

static void draw_playlist_info()
{
    assert(ctx.center.playlist != nullptr);

    LruCacheRef<Playlist>& playlist = ctx.center.playlist;
    std::vector<SongAlbumArtistTrack>& tracks = ctx.center.song_tracks;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);

    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_playlist(ctx.center.playlist->id);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();
    ImVec2 region_start(
            origin.x + album_art_size.x + 2 * album_art_padding,
            origin.y + album_art_padding);
    ImGui::SetCursorPos(region_start);
    ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

    ImGui::BeginChild("playlist_view", region_size);
    region_start = ImGui::GetCursorPos();

    imgui_push_font_scale(2.0f); 
    imgui_aligned_text(playlist->name.c_str(), region_start, region_size);
    imgui_pop_font_scale(); 

    imgui_push_font_scale(1.5f); 
    int length = static_cast<int>(playlist->length);
    ImGui::Text("%d:%02d", length / 60, length % 60);
    imgui_pop_font_scale(); 

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
    ImVec2 button_size(32.0f, 32.0f);
    ImGui::SetCursorPosY(region_start.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);
    if (imgui_image_button(ctx.textures.play_button.id, button_size))
    {
        mp_ctx.shuffle = false;
        mp_play_playlist(playlist->id);
    }
    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.shuffle_button.id, button_size))
    {
        mp_ctx.shuffle = true;
        mp_play_playlist(playlist->id);
    }
    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.show_queue_button.id, button_size))
        for (const SongAlbumArtistTrack& song : tracks)
            mp_queue_song(song.song->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pin_button.id, button_size, (ctx.center.pinned) ? RED : WHITE))
        ctx.center.pinned = mp_toggle_pin_playlist(playlist->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size, (ctx.center.show == SHOW_EDIT) ? RED : WHITE))
    {
        if (ctx.center.show == SHOW_EDIT)
            ctx.center.show = SHOW_NONE;
        else
            ctx.center.show = SHOW_EDIT;
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    imgui_separator();
    ImGui::BeginChild("more_album_info", ImGui::GetContentRegionAvail());
    if (ctx.center.show == SHOW_EDIT)
        draw_playlist_info_edit();
    else
        draw_playlist_info_tracklist();
    ImGui::EndChild();
}

static void draw_artist_info_songs()
{
    LruCacheRef<Song> center_song = nullptr;
    LruCacheRef<Artist> center_artist = nullptr;
    LruCacheRef<Album> center_album = nullptr;

    CustomTableParams params{};
    params.num_cols = 3;
    params.num_rows = static_cast<int>(ctx.center.song_search_result.entries.size());
    params.header_row_height = 40.0f;
    params.row_height = 48.0f;
    params.table_width = ImGui::GetContentRegionAvail().x;

    static float name_col_offset = 10.0f;
    static float album_col_offset = 550.0f;
    static float length_col_right_offset = 100.0f;
    float length_col_offset = params.table_width - length_col_right_offset;

    static float name_col_width = 300.0f;
    static float album_col_width = 300.0f;
    static float length_col_width = 50.0f;

    static int selected_row = -1;

    params.selected_row = &selected_row;
    params.menu_popup_callback = [](int* selected_row) -> MenuPopupEnum
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[*selected_row].song;
            if (ImGui::Button("Show"))
            {
                *selected_row = -1;
                set_center_view_song(mp_get_song(song->id));
                return MENU_CLOSE_TABLE;
            }
            if (ImGui::Button("Play"))
            {
                mp_play_song(song->id);
                return MENU_CLOSE_POPUP;
            }
            if (ImGui::Button("Queue"))
            {
                mp_queue_song(song->id);
                return MENU_CLOSE_POPUP;
            }
            draw_add_to_playlist_popup(song->id);
            if (ImGui::Button("Close"))
                return MENU_CLOSE_POPUP;

            return MENU_END_POPUP;
        };

    params.col_offsets.push_back(&name_col_offset);
    params.col_widths.push_back(name_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Title", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [&center_song](ImVec2 region_start, ImVec2 region_size, int row)
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row].song;
            ImVec2 picture_size(48, 48);
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::SetCursorPos(region_start);
            const bool hovered = imgui_is_region_hovered(ImGui::GetCursorScreenPos(), picture_size);
            ImVec4 tint = (hovered) ? GRAY : WHITE;
            ImGui::ImageWithBg(tex.id, picture_size, tex.uv0, tex.uv1, INVISIBILE, tint);
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                GLTexture tex = ctx.textures.play_button;
                ImVec2 play_button_size(18.0f, 18.0f);
                ImVec2 play_button_pos(
                        region_start.x + (picture_size.x - play_button_size.x) / 2.0f,
                        region_start.y + (picture_size.y - play_button_size.y) / 2.0f);
                ImGui::SetCursorPos(play_button_pos);
                ImGui::PushStyleColor(ImGuiCol_Button, BLACK);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BLACK);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, BLACK);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                const ImVec4 bg_col = INVISIBILE;
                const ImVec4 tint_col = WHITE;
                // IDK why ImGui::Image() doesnt work here maybe investigate some other time
                ImGui::ImageButton("Play", tex.id, play_button_size, UV0, UV1, bg_col, tint_col);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                        mp_queue_song(song->id);
                    else
                        mp_play_song(song->id);
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
            }
            float padding = 5.0f;
            bool pressed = imgui_aligned_text_button(song->name.c_str(), ImVec2(region_start.x + picture_size.x + padding, region_start.y), ImVec2(region_size.x - picture_size.x - padding, region_size.y), ALIGN_LEFT, ALIGN_CENTER);
            if (pressed)
                center_song = mp_get_song(song->id);
        });

    params.col_offsets.push_back(&album_col_offset);
    params.col_widths.push_back(album_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Album", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [&center_album](ImVec2 region_start, ImVec2 region_size, int row)
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row].song;
            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (album == nullptr)
                return;
            if (imgui_aligned_text_button(album->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                center_album = mp_get_album(album->id);
        });

    params.col_offsets.push_back(&length_col_offset);
    params.col_widths.push_back(length_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Length", region_start, region_size, ALIGN_RIGHT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size, int row)
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row].song;
            char length_str[256];
            int length = static_cast<int>(song->length);
            snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
            imgui_aligned_text(length_str, region_start, region_size, ALIGN_RIGHT, ALIGN_CENTER);
        });

    imgui_custom_table(params);

    length_col_right_offset = params.table_width - length_col_offset;

    if (center_song != nullptr)
        set_center_view_song(std::move(center_song));
    if (center_artist != nullptr)
        set_center_view_artist(std::move(center_artist));
    if (center_album != nullptr)
        set_center_view_album(std::move(center_album));
}

static void draw_artist_info_edit_merge()
{
    const float padding = 10.0f;
    int page_num = ctx.center.artist_search_result.page_num;
    int num_pages = ctx.center.artist_search_result.num_pages;
    int num_results = ctx.center.artist_search_result.num_results;

    ImGui::SetCursorPos(ImVec2(padding, padding));
    if (ImGui::Button("Close"))
        ctx.center.show_split = SHOW_NONE;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##search_box", "Search...", ctx.search_query, sizeof(ctx.search_query)))
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 0.0f));

    ImGui::SameLine();
    if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, --ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num > num_pages)
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, ++ctx.center.artist_search_result.page_num);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGui::SameLine();
    ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
    ImGui::Separator();

    ImGui::PopStyleVar();

    ImVec2 region_start = ImGui::GetCursorPos();
    region_start.x += 10.0f;
    region_start.y += 10.0f;
    ImVec2 region_size = ImGui::GetContentRegionAvail();
    imgui_push_font_scale(1.5f);
    ImVec2 image_size = ImVec2(64, 64);
    float spacing = 10.0f;
    for (LruCacheRef<Artist>& test_artist : ctx.center.artist_search_result.entries)
    {
        ImGui::PushID(test_artist->id);
        GLTexture tex = get_texture_from_artist(test_artist->id);
        float text_height = ImGui::CalcTextSize(test_artist->name.c_str()).y;
        ImGui::SetCursorPos(region_start);
        imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
        if (imgui_aligned_text_button(test_artist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
        {
            ArtistID dst_artist_id = test_artist->id; 
            ArtistID src_artist_id = ctx.center.artist->id;
            ctx.center.artist.release();
            ctx.center.artist_search_result.clear();
            ctx.center.added_artists.clear();
            mp_merge_artists(dst_artist_id, src_artist_id);
            set_center_view_artist(mp_get_artist(dst_artist_id));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        region_start.y += image_size.y + spacing;
    }
    imgui_pop_font_scale();
}

static void draw_artist_info_edit()
{
    static bool set_active;
    const bool split = ctx.center.show_split != SHOW_NONE;

    ImGui::BeginTable("table", 1 + split, ImGuiTableFlags_Resizable);
    ImGui::TableNextColumn();

    ImGui::BeginChild("child1", ImGui::GetContentRegionAvail());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK_GRAY);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    float col_1_cursor_x = 10.0f;
    float col_2_cursor_x = 120.0f;
    imgui_push_font_scale(1.5f); 
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Artist", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.artist[0] = '\0';
        ctx.center.edit.changed_info = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    const bool artist_name_empty = ctx.center.edit.artist[0] == '\0';
    if (artist_name_empty)
        ImGui::PushStyleColor(ImGuiCol_Text, CYAN);
    if (ctx.center.edit.edit_type == EDIT_ARTIST_NAME)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - col_1_cursor_x);
        if (set_active)
        {
            ImGui::SetKeyboardFocusHere(0);
            set_active = false;
        }
        ctx.center.edit.changed_info |= ImGui::InputText("##Artist", ctx.center.edit.artist, sizeof(ctx.center.edit.artist));
        if (ImGui::IsItemDeactivated())
            ctx.center.edit.edit_type = EDIT_NONE;
    }
    else if (imgui_aligned_text_button((artist_name_empty) ? EMPTY_STR : ctx.center.edit.artist, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.edit_type = EDIT_ARTIST_NAME;
        set_active = true;
    }
    if (artist_name_empty)
        ImGui::PopStyleColor();

    GLTexture edit_tex = get_texture_from_slot(ctx.textures.edit_slot);
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (imgui_aligned_text_button("Cover", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
    {
        ctx.center.edit.art.clear();
        copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_artist_slot);
        ctx.center.edit.changed_info = true;
        ctx.center.edit.changed_art = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(col_2_cursor_x);
    if (imgui_image_button(edit_tex.id, LARGE_IMAGE_SIZE, WHITE, edit_tex.uv0, edit_tex.uv1) && !ctx.prompt.has_value())
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string name{"Choose file"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"JPG", "*.jpg"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> path = pfd::open_file(name, default_path, filters, options).result();
                if (path.size() > 0)
                {
                    int width, height, nc;
                    uint8_t* raw_art_data = stbi_load(path.front().c_str(), &width, &height, &nc, 4);
                    ctx.center.edit.art.clear();
                    ctx.center.edit.art.data = stbir_resize_uint8_linear(
                                raw_art_data, width, height, 0,
                                NULL, ART_WIDTH, ART_WIDTH, 0,
                                STBIR_4CHANNEL
                            );
                    ctx.center.edit.art.size = ART_WIDTH * ART_WIDTH * 4 * sizeof(uint8_t);
                    stbi_image_free(raw_art_data);
                    return std::move(path.front());
                }
                return std::string{};
            });
        ctx.prompt_callback = 
            []()
            {
                std::string path = ctx.prompt->get();
                if (path.size() > 0)
                {
                    update_texture_slot(ctx.textures.edit_slot, ctx.center.edit.art.data);
                    ctx.center.edit.changed_info = true;
                    ctx.center.edit.changed_art = true;
                }
                else
                {
                    SPDLOG_INFO("Choose album art cancelled");
                }
            };
    }

    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Save"))
    {
        Artist artist{};
        artist.id = ctx.center.artist->id;
        artist.name = ctx.center.edit.artist;
        mp_update_artist(artist);
        if (ctx.center.edit.changed_art)
        {
            mp_update_artist_art(ctx.center.artist->id, ctx.center.edit.art);
            MPUID uid = mp_get_artist_uid(ctx.center.artist->id);
            if (ctx.center.edit.reset_art)
            {
                if (mpuid_has_valid_texture(uid))
                    delete_texture(ctx.textures.slot_map[uid]);
                ctx.textures.slot_map[uid] = ctx.textures.default_artist_slot;
            }
            else
            {
                if (mpuid_has_valid_texture(uid))
                    copy_texture_slot(ctx.textures.slot_map[uid], ctx.textures.edit_slot);
                else
                    ctx.textures.slot_map[uid] = create_texture(ctx.center.edit.art.data);
            }
            ctx.center.edit.changed_art = false;
        }
        ctx.center.edit.changed_info = false;
    }
    if (ctx.center.edit.changed_info)
    {
        ImGui::SameLine();
        ImGui::Text(" [Unsaved Changes]");
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Reset"))
    {
        snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());

        MPUID uid = mp_get_artist_uid(ctx.center.artist->id);
        if (mpuid_has_valid_texture(uid))
        {
            ctx.center.edit.art = mp_get_artist_art(ctx.center.artist->id);
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.slot_map[uid]);
        }
        else
        {
            copy_texture_slot(ctx.textures.edit_slot, ctx.textures.default_artist_slot);
        }
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Delete"))
    {
        ArtistID artist_id = ctx.center.artist->id;
        set_center_view_home();
        mp_delete_artist(artist_id);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        imgui_pop_font_scale();
        ImGui::EndChild();
        ImGui::EndTable();
        return;
    }
    ImGui::SetCursorPosX(col_1_cursor_x);
    if (ImGui::Button("Merge"))
    {
        ctx.center.show_split = SHOW_ARTIST_MERGE;
        ctx.center.artist_search_result = mp_search_artists(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    imgui_pop_font_scale(); 
    ImGui::EndChild();

    if (split)
    {
        ImGui::TableNextColumn();
        ImGui::BeginChild("child2", ImGui::GetContentRegionAvail());
        draw_artist_info_edit_merge();
        ImGui::EndChild();
    }
    ImGui::EndTable();
}

static void draw_artist_info_tracklist()
{
    static int current_search_type = static_cast<int>(SEARCH_TYPE_SONG);
    const float padding = 10.0f;
    const float header_height = 36.0f;

    const char* search_types[] = {
        "Songs",
        "Albums"
    };

    ImGui::SetCursorPos(ImVec2(0.0f, padding + header_height));
    if (current_search_type == SEARCH_TYPE_SONG)
    {
        draw_artist_info_songs();
        ImGui::SetCursorPos(ImVec2(padding, padding));
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##search_combo", &current_search_type, search_types, std::size(search_types)))
            if (current_search_type == SEARCH_TYPE_ALBUM)
                ctx.center.album_search_result = mp_get_paginated_albums_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, 1);

        ImGui::SameLine();
        if (ImGui::ArrowButton("Left", ImGuiDir_Left) && ctx.center.song_search_result.page_num > 1)
            ctx.center.song_search_result = mp_get_paginated_songs_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, --ctx.center.song_search_result.page_num);

        ImGui::SameLine();
        if (ImGui::ArrowButton("Right", ImGuiDir_Right) && ctx.center.song_search_result.page_num < ctx.center.song_search_result.num_pages)
            ctx.center.song_search_result = mp_get_paginated_songs_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, ++ctx.center.song_search_result.page_num);

        ImGui::SameLine();
        ImGui::Text("%d/%d (%d results)", ctx.center.song_search_result.page_num, ctx.center.song_search_result.num_pages, ctx.center.song_search_result.num_results);
    }
    else
    {
        imgui_separator();
        draw_album_table(ctx.center.albums);

        ImGui::SetCursorPos(ImVec2(padding, padding));
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##search_combo", &current_search_type, search_types, std::size(search_types)))
            if (current_search_type == SEARCH_TYPE_SONG)
                ctx.center.song_search_result = mp_get_paginated_songs_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, 1);
    }
}

static void draw_artist_info()
{
    assert(ctx.center.type == SHOW_CENTER_ARTIST);

    LruCacheRef<Artist>& artist = ctx.center.artist;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);

    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_artist(artist->id);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();

    ImVec2 region_start(
            origin.x + album_art_size.x + 2 * album_art_padding,
            origin.y + album_art_padding);
    ImGui::SetCursorPos(region_start);
    ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

    ImGui::BeginChild("artist_view", region_size);
    imgui_push_font_scale(2.0f); 
    const char* artist_name = (artist->name.size() > 0) ? artist->name.c_str() : "<No Artist>";
    imgui_aligned_text(artist_name, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
    imgui_pop_font_scale();

    const ImVec2 button_size = ImVec2(32, 32);
    ImGui::SetCursorPosY(origin.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);
    if (imgui_image_button(ctx.textures.pin_button.id, button_size, (ctx.center.pinned) ? RED : WHITE))
        ctx.center.pinned = mp_toggle_pin_artist(artist->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size, (ctx.center.show == SHOW_EDIT) ? RED : WHITE))
    {
        if (ctx.center.show == SHOW_EDIT)
            ctx.center.show = SHOW_NONE;
        else
            ctx.center.show = SHOW_EDIT;
    }

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    imgui_separator();

    ImGui::BeginChild("##artist", ImGui::GetContentRegionAvail());
    if (ctx.center.show)
        draw_artist_info_edit();
    else
        draw_artist_info_tracklist();
    ImGui::EndChild();
}

static void pop_history()
{
    ctx.view_history.pop_back();
    if (ctx.view_history.empty())
    {
        ctx.center.type = SHOW_CENTER_NONE;
        return;
    }
    auto [view_type, id] = ctx.view_history.back();

    if (view_type == SHOW_CENTER_SONG)
        set_center_view_song(mp_get_song(id));
    else if (view_type == SHOW_CENTER_SONG)
        set_center_view_album(mp_get_album(id));
    else if (view_type == SHOW_CENTER_PLAYLIST)
        set_center_view_playlist(mp_get_playlist(id));
    else if (view_type == SHOW_CENTER_ARTIST)
        set_center_view_artist(mp_get_artist(id));
}

static void draw_header(ImVec2 size)
{
    const float advance = 10.0f;
    ImVec2 button_size = ImVec2(32.0f, 32.0f);

    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + advance, (size.y - button_size.y) / 2.0f));

    ImVec4 tint = (ctx.quick_access_open) ? SELECTED_COLOR_1 : WHITE;
    if (imgui_image_button(ctx.textures.pin_button.id, button_size, tint))
        ctx.quick_access_open = !ctx.quick_access_open;


    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    tint = (ctx.center.type == SHOW_CENTER_HOME) ? SELECTED_COLOR_1 : WHITE;
    if (imgui_image_button(ctx.textures.home_button.id, button_size, tint))
        set_center_view_home();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    tint = (ctx.center.type == SHOW_CENTER_SETTINGS) ? SELECTED_COLOR_1 : WHITE;
    if (imgui_image_button(ctx.textures.settings_button.id, button_size, tint))
        set_center_view_settings();
    
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    tint = (ctx.center.type == SHOW_CENTER_EDIT) ? SELECTED_COLOR_1 : WHITE;
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size, tint))
        set_center_view_edit();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    tint = (ctx.center.type == SHOW_CENTER_SEARCH) ? SELECTED_COLOR_1 : WHITE;
    if (imgui_image_button(ctx.textures.search.id, button_size, tint))
        set_center_view_search_result();

    ImGui::SameLine();

    const bool history_non_empty = ctx.view_history.empty();
    if (history_non_empty)
        ImGui::BeginDisabled();

    ImGui::SetCursorPos(ImVec2(size.x - button_size.x * 2 - advance * 3, (size.y - button_size.y) / 2.0f));
    if (imgui_image_button(ctx.textures.play_button.id, button_size, WHITE, UV0_INVERTED_HORIZONTALLY, UV1_INVERTED_HORIZONTALLY))
        pop_history();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    if (imgui_image_button(ctx.textures.play_button.id, button_size))
        pop_history();

    if (history_non_empty)
        ImGui::EndDisabled();
}

static void draw_home()
{
    ImGui::Text("Home");

    if (ImGui::Button("Change DB")) 
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string name {"Choose database"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"SQLite DB", "*.db"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> db_path = pfd::open_file(name, default_path, filters, options).result();
                return (db_path.size() > 0) ? std::move(db_path.front()) : std::string{};
            });
        ctx.prompt_callback = []()
            {
                std::string db_path = ctx.prompt->get();
                if (db_path.size() > 0)
                {
                    destroy_cache_references();
                    mp_change_db(db_path);
                }
                else
                {
                    SPDLOG_INFO("Change db prompt cancelled");
                }
            };
    }

    ImGui::Text("DB: %s", config.db_path.c_str());
    ImGui::Text("# Songs = %d", mp_ctx.num_songs);
    ImGui::Text("# Albums = %d", mp_ctx.num_albums);
    ImGui::Text("# Artists = %d", mp_ctx.num_artists);
    ImGui::Text("# Playlists = %d", mp_ctx.num_playlists);
}

static void draw_settings()
{
    ImGui::Text("Settings");
}

static void draw_edit()
{
    const char* search_types[] = {
        "Songs",
        "Albums",
        "Artist"
    };

    float padding = 10.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
    if (imgui_button("Add Song")) 
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string
            {
                const std::string name {"Choose files to read"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"All Files", "*"};
                const pfd::opt options = pfd::opt::multiselect;
                std::vector<std::string> song_paths = pfd::open_file(name, default_path, filters, options).result();
                mp_add_songs(std::move(song_paths));
                return {};
            });
        ctx.prompt_callback = []()
            {
                assert(!mp_ctx.adding_songs);
                ctx.center.edit.added_songs_search_type = static_cast<int>(SEARCH_TYPE_SONG);
                ctx.center.added_songs.page_num = 1;
                refresh_added_songs();
            };
    }
    ImGui::SameLine();
    if (imgui_button("Add Folder")) 
    {
        ctx.prompt = std::async(std::launch::async,
            []() -> std::string
            {
                const std::string name {"Select Any Directory"};
                const std::string default_path = pfd::path::home();
                const pfd::opt options = pfd::opt::multiselect;
                const std::string folder_path = pfd::select_folder(name, default_path, options).result();
                mp_recursive_add_songs(folder_path);
                return {};
            });
        ctx.prompt_callback = []()
            {
                assert(!mp_ctx.adding_songs);
                ctx.center.edit.added_songs_search_type = static_cast<int>(SEARCH_TYPE_SONG);
                ctx.center.added_songs.page_num = 1;
                refresh_added_songs();
            };
    }

    ImGui::SameLine();
    if (imgui_button("Create Song"))
    {
        set_center_view_song(mp_create_song());
        return;
    }
    ImGui::SameLine();
    if (imgui_button("Create Album"))
    {
        set_center_view_album(mp_create_album());
        return;
    }
    ImGui::SameLine();
    if (imgui_button("Create Artist"))
    {
        set_center_view_artist(mp_create_artist());
        return;
    }
    ImGui::SameLine();
    if (imgui_button("Create Playlist"))
    {
        set_center_view_playlist(mp_create_playlist());
        return;
    }
    ImGui::SameLine();
    if (imgui_button("Reset"))
    {
        mp_reset_added_songs();
        mp_ctx.added_songs.clear();
        mp_ctx.added_albums.clear();
        mp_ctx.added_artists.clear();
    }

    ImGui::SetCursorPosX(0.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding - ImGui::GetStyle().ItemSpacing.y);
    imgui_separator();

    if (mp_ctx.adding_songs)
    {
        padding = 4.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        std::lock_guard lock{mp_ctx.adding_songs_mutex};
        char string[1024];
        snprintf(string, sizeof(string), "%d / %d", mp_ctx.num_songs_added, mp_ctx.num_songs_to_add);
        ImGui::SetCursorPosX(padding);
        imgui_aligned_text(string, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        snprintf(string, sizeof(string), "%s", mp_ctx.adding_songs_current_path.c_str());
        ImGui::SetCursorPosX(padding);
        imgui_aligned_text(string, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
    }
    else
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##search_combo", &ctx.center.edit.added_songs_search_type, search_types, std::size(search_types)))
        {
            refresh_added_songs();
        }

        int page_num{};
        int num_pages{};
        int num_results{};
        if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_SONG)
        {
            page_num = ctx.center.added_songs.page_num;
            num_pages = ctx.center.added_songs.num_pages;
            num_results = ctx.center.added_songs.num_results;
        }
        else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ALBUM)
        {
            page_num = ctx.center.added_albums.page_num;
            num_pages = ctx.center.added_albums.num_pages;
            num_results = ctx.center.added_albums.num_results;
        }
        else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ARTIST)
        {
            page_num = ctx.center.added_artists.page_num;
            num_pages = ctx.center.added_artists.num_pages;
            num_results = ctx.center.added_artists.num_results;
        }

        ImGui::SameLine();
        if (ImGui::ArrowButton("Left", ImGuiDir_Left) && page_num > 1)
        {
            if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_SONG)
                --ctx.center.added_songs.page_num;
            else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ALBUM)
                --ctx.center.added_albums.page_num;
            else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ARTIST)
                --ctx.center.added_artists.page_num;
            refresh_added_songs();
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("Right", ImGuiDir_Right) && page_num < num_pages)
        {
            if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_SONG)
                ++ctx.center.added_songs.page_num;
            else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ALBUM)
                ++ctx.center.added_albums.page_num;
            else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ARTIST)
                ++ctx.center.added_artists.page_num;
            refresh_added_songs();
        }
        ImGui::SameLine();
        ImGui::Text("%d/%d (%d results)", page_num, num_pages, num_results);

        ImGui::SetCursorPosX(0.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding - ImGui::GetStyle().ItemSpacing.y);
        if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_SONG)
        {
            static float name_col_offset = 10.0f;
            static float artist_col_offset = 300.0f;
            static float album_col_offset = 550.0f;
            static float length_col_right_offset = 100.0f;

            DrawSongTableParams params{};
            params.songs = &ctx.center.added_songs.entries;
            params.column_offsets = {&name_col_offset, &artist_col_offset, &album_col_offset, &length_col_right_offset};

            draw_song_table(params);
        }
        else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ALBUM)
        {
            imgui_separator();
            draw_album_table(ctx.center.added_albums.entries);
        }
        else if (ctx.center.edit.added_songs_search_type == SEARCH_TYPE_ARTIST)
        {
            imgui_separator();
            draw_artist_table(ctx.center.added_artists.entries);
        }
    }
}

static void draw_lyrics()
{
    const float margin = 10.0f;
    imgui_push_font_scale(1.5f); 
    if (mp_ctx.current_song_lyrics[0] == '\0')
    {
        imgui_aligned_text("No Lyrics", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail(), ALIGN_CENTER, ALIGN_CENTER);
    }
    else
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + margin);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + margin);
        ImGui::TextWrapped("%s", mp_ctx.current_song_lyrics);
    }
    imgui_pop_font_scale(); 
}

static void draw_center()
{
    switch (ctx.center.type)
    {
        case SHOW_CENTER_HOME:
            draw_home();
            break;
        case SHOW_CENTER_SETTINGS:
            draw_settings();
            break;
        case SHOW_CENTER_EDIT:
            draw_edit();
            break;
        case SHOW_CENTER_LYRICS:
            draw_lyrics();
            break;
        case SHOW_CENTER_SONG:
            draw_song_info();
            break;
        case SHOW_CENTER_ALBUM:
            draw_album_info();
            break;
        case SHOW_CENTER_PLAYLIST:
            draw_playlist_info();
            break;
        case SHOW_CENTER_ARTIST:
            draw_artist_info();
            break;
        case SHOW_CENTER_SEARCH:
            draw_search_results();
            break;
        default:
            break;
    }
}

static void draw_queue()
{
    float padding = 8.0f;
    ImGui::SetCursorPosX(padding);
    imgui_push_font_scale(2.0f); 
    ImGui::Text("Queue");
    imgui_pop_font_scale(); 
    int row = 0;
    ImVec2 root_pos = ImGui::GetCursorPos();
    root_pos.x = padding;
    float row_advance = 72.0f;
    auto list_song = [&row, &root_pos, &row_advance](const LruCacheRef<Song>& song, const LruCacheRef<Album>& album, const LruCacheRef<Artist>& artist, const ImVec4& color)
        {
            ImGui::SetCursorPos(ImVec2(root_pos.x, root_pos.y + row * row_advance));
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, color);
            ImGui::Image(tex.id, SMALL_IMAGE_SIZE, tex.uv0, tex.uv1);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImVec2 cursor_pos = ImGui::GetCursorPos();
            if (imgui_aligned_text_button(song->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                set_center_view_song(mp_get_song(song->id));
            ImVec2 text_size = ImGui::CalcTextSize(song->name.c_str());
            cursor_pos.y += text_size.y;
            ImGui::SetCursorPos(cursor_pos);
            if (artist != nullptr)
            {
                if (imgui_aligned_text_button(artist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    set_center_view_artist(mp_get_artist(artist->id));
                cursor_pos.y += text_size.y;
            }
            ImGui::SetCursorPos(cursor_pos);
            if (album != nullptr)
            {
                if (imgui_aligned_text_button(album->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    set_center_view_album(mp_get_album(album->id));
                cursor_pos.y += text_size.y;
            }

            row++;
        };

    for (const SongAlbumArtist& song : mp_ctx.queue)
        list_song(song.song, song.album, song.artist, RED);

    for (const SongAlbumArtistTrack& song_track : mp_ctx.group_queue)
        list_song(song_track.song, song_track.album, song_track.artist, GREEN);
    
    for (const SongAlbumArtist& song : mp_ctx.autoplay_queue)
        list_song(song.song, song.album, song.artist, BLUE);
}

static void draw_player(const ImVec2 size)
{
    ImGui::SetCursorPos(ImVec2(14, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImVec2 album_art_size = ImVec2(100, 100);
    GLTexture tex = (mp_ctx.current_song.song != nullptr) 
        ? get_texture_from_song(mp_ctx.current_song.song->id)
        : get_texture_from_slot(ctx.textures.default_album_slot);
    ImGui::ImageWithBg(tex.id, album_art_size, tex.uv0, tex.uv1, BLACK);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    const char* song_text = (mp_ctx.current_song.song == nullptr)
        ? "No Song Playing"
        : mp_ctx.current_song.song->name.c_str();

    constexpr float padding = 3.0f;

    ImVec2 song_info_offset = ImVec2(132.0f, 25.0f);
    float cursor_width = 450.0f;
    float song_cursor_x = (size.x - cursor_width) / 2.0f;

    ImGui::SetCursorPos(song_info_offset);
    {
        const ImVec2 region_start = song_info_offset;
        const ImVec2 region_size = ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y);
        if (mp_ctx.current_song.song != nullptr)
        {
            if (imgui_aligned_text_button(song_text, region_start, region_size, ALIGN_LEFT, ALIGN_LEFT))
                set_center_view_song(mp_get_song(mp_ctx.current_song.song->id));
        }
        else
            imgui_aligned_text(song_text, region_start, region_size, ALIGN_LEFT, ALIGN_LEFT);
    }

    song_info_offset.y += ImGui::CalcTextSize(song_text).y + padding;

    if (mp_ctx.current_song.artist != nullptr)
    {
        const char* artist_name = mp_ctx.current_song.artist->name.c_str();
        if (imgui_aligned_text_button(artist_name, song_info_offset, ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y), ALIGN_LEFT, ALIGN_LEFT))
            set_center_view_artist(mp_get_artist(mp_ctx.current_song.artist->id));
        song_info_offset.y += ImGui::CalcTextSize(artist_name).y + padding;
    }
    if (mp_ctx.current_song.album != nullptr)
    {
        const char* album_name = mp_ctx.current_song.album->name.c_str();
        ImGui::SetCursorPos(song_info_offset);
        if (imgui_aligned_text_button(album_name, song_info_offset, ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y), ALIGN_LEFT, ALIGN_LEFT))
            set_center_view_album(mp_get_album(mp_ctx.current_song.album->id));
    }

    char cursor_str[256];
    int cursor = static_cast<int>(mp_ctx.current_song_cursor);
    int length = static_cast<int>(mp_ctx.current_song_length);
    snprintf(cursor_str, sizeof(cursor_str), "%d:%02d / %d:%02d", cursor / 60, cursor % 60, length / 60, length % 60);
    ImGui::SetCursorPos(ImVec2(song_cursor_x, 15.0f));
    ImGui::Text("%s", cursor_str);
    ImGui::SetCursorPos(ImVec2(song_cursor_x, 15.0f + ImGui::CalcTextSize(cursor_str).y + 5.0f));
    if (imgui_custom_slider_float("##cursor", &mp_ctx.current_song_cursor, 0.0f, mp_ctx.current_song_length, ImVec2(cursor_width, 5.0f)))
        mp_update_cursor();

    const ImVec2 button_size = ImVec2(32.0f, 32.0f);
    float spacing = 20.0f;
    int num_buttons = 7;
    float width = 32.0f * num_buttons + spacing * (num_buttons - 1);
    float advance = button_size.x + spacing;
    float cursor_x = (size.x - width) / 2.0f;
    float cursor_y = 60.0f;
    constexpr float skip_time = 5.0f;
    ImVec4 tmp_col = WHITE;

    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    {
        ImVec4 tint{};
        if (mp_ctx.loop_mode == LOOP_GROUP)
            tint = SELECTED_COLOR_1;
        else if (mp_ctx.loop_mode == LOOP_TRACK)
            tint = SELECTED_COLOR_2;
        else
            tint = UNSELECTED_COLOR;
        if (imgui_image_button(ctx.textures.repeat_button.id, button_size, tint))
            mp_ctx.loop_mode = static_cast<LoopMode>((static_cast<int>(mp_ctx.loop_mode) + 1) % 3);
    }

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    if (imgui_image_button(ctx.textures.round_rewind_button.id, button_size, tmp_col))
        mp_go_backward_in_current_song(skip_time);

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    if (imgui_image_button(ctx.textures.skip_button.id, button_size, tmp_col, UV0_INVERTED_HORIZONTALLY, UV1_INVERTED_HORIZONTALLY))
        mp_play_previous();

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    tex = (mp_ctx.paused) ? ctx.textures.play_button : ctx.textures.pause_button;
    const bool key_pressed = !ImGui::GetIO().WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_F9));
    if (imgui_image_button(tex.id, button_size, tmp_col) || key_pressed)
        mp_pause_or_resume();

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    if (imgui_image_button(ctx.textures.skip_button.id, button_size, tmp_col))
        mp_play_next();

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    if (imgui_image_button(ctx.textures.round_rewind_button.id, button_size, tmp_col, UV0_INVERTED_HORIZONTALLY, UV1_INVERTED_HORIZONTALLY))
        mp_go_forward_in_current_song(skip_time);

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
    {
        ImVec4 tint = (mp_ctx.shuffle)
            ? SELECTED_COLOR_1
            : UNSELECTED_COLOR;
        if (imgui_image_button(ctx.textures.shuffle_button.id, button_size, tint))
            mp_toggle_shuffle();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        GLTexture tex = (mp_ctx.mute) ? ctx.textures.mute : ctx.textures.volume;
        float right_edge_spacing = 10.0f;
        float icon_spacing = 4.0f;
        ImVec2 volume_icon_size = ImVec2(20.0f, 20.0f);
        float slider_y_offset = 30.0f;
        ImVec2 slider_size = ImVec2(162.0f, 5.0f);
        ImGui::SetCursorPos(ImVec2(size.x - slider_size.x - volume_icon_size.x - right_edge_spacing - icon_spacing, slider_y_offset));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (imgui_image_button(tex.id, volume_icon_size))
            mp_toggle_mute();
        ImGui::PopStyleVar();

        ImGui::SetCursorPos(ImVec2(size.x - slider_size.x - right_edge_spacing, slider_y_offset + (volume_icon_size.x - slider_size.y) / 2.0f));
        if (imgui_custom_slider_float("##volume", &mp_ctx.volume, 0.0f, 1.0f, slider_size))
        {
            if (mp_ctx.current_song.song != nullptr)
                mp_update_volume(mp_ctx.current_song.song->volume);
        }

        num_buttons = 4;
        width = button_size.x * num_buttons + spacing * (num_buttons - 1);
        cursor_x = size.x - width - right_edge_spacing;

        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        {
            ImVec4 tint = (mp_ctx.autoplay)
                ? SELECTED_COLOR_1
                : UNSELECTED_COLOR;
            if (imgui_image_button(ctx.textures.autoplay_button.id, button_size, tint))
                mp_toggle_autoplay();
        }
        
        cursor_x += advance;
        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        if (imgui_image_button(ctx.textures.lyrics_button.id, button_size, (ctx.center.type == SHOW_CENTER_LYRICS) ? RED : WHITE))
            set_center_view_lyrics();

        cursor_x += advance;
        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        if (imgui_image_button(ctx.textures.show_queue_button.id, button_size))
            mp_queue_clear();
        ImVec2 x_size = ImVec2(12.0f, 12.0f);
        ImGui::SetCursorPos(ImVec2(cursor_x + button_size.x - x_size.x, cursor_y + button_size.y - x_size.y));
        ImGui::ImageWithBg(ctx.textures.close_button.id, x_size, UV0, UV1, INVISIBILE, RED);

        cursor_x += advance;
        ImVec4 tint = (ctx.queue_open) 
            ? SELECTED_COLOR_1
            : UNSELECTED_COLOR;
        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        if (imgui_image_button(ctx.textures.show_queue_button.id, button_size, tint))
            toggle_right_view_queue();

        ImGui::PopStyleVar();
    }
}

#ifndef NDEBUG
static void draw_debug_info()
{
    ImGui::PushStyleColor(ImGuiCol_Border, GRAY);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &ctx.show_debug_window))
    {
        ImGui::Text("%5.3f", ctx.debug.fps / 1000.0f);
        ImGui::Text("Song History size: %zu", mp_ctx.song_history.size());
        ImGui::Text("Max num slots: %d", ctx.textures.num_slots);
        ImGui::Text("Num slots in use: %zd", ctx.textures.num_slots - ctx.textures.free_slot_textures.size());

        static int log_level = SPDLOG_LEVEL_TRACE;
        const char* log_levels[] = {
            "TRACE",
            "DEBUG",
            "INFO",
            "WARN",
            "ERROR",
            "CRITICAL",
            "OFF"
        };
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Log Level", &log_level, log_levels, std::size(log_levels)))
            spdlog::set_level(static_cast<spdlog::level::level_enum>(log_level));

        if (ctx.textures.textures.size() == 0)
        {
            ImGui::Text("No Textures");
        }
        else
        {
            static int texture_num = 0;
            if (ImGui::ArrowButton("Left", ImGuiDir_Left))
                texture_num--;
            ImGui::SameLine();
            if (ImGui::ArrowButton("Right", ImGuiDir_Right))
                texture_num++;
            texture_num = std::clamp(texture_num, 0, static_cast<int>(ctx.textures.textures.size()) - 1);
            ImGui::SameLine();
            ImGui::Text("Texture %d", texture_num + 1);
            ImGui::Image(ctx.textures.textures[texture_num], ImVec2(512, 512));
        }

        ImGui::End();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
#endif

static void draw_pins()
{
    int row = 0;
    float row_advance = 72.0f;

    float padding = 10.0f;
    ImGui::SetCursorPosX(padding);

    imgui_push_font_scale(2.0f); 
    ImGui::Text("Pins");
    imgui_pop_font_scale(); 

    ImVec2 root_pos = ImGui::GetCursorPos();

    MPUID updated_pin_uid = INVALID_UID;

    auto list_pin = [&row, &root_pos, &row_advance, &updated_pin_uid, padding](const Pin& pin)
        {
            GLTexture tex = get_texture_from_slot(ctx.textures.default_album_slot);
            ImVec4 color = WHITE;
            if (pin.type == MPUID_SONG)
            {
                tex = get_texture_from_song(pin.song->id);
                color = YELLOW;
            }
            if (pin.type == MPUID_ALBUM)
            {
                tex = get_texture_from_album(pin.album->id);
                color = PURPLE;
            }
            if (pin.type == MPUID_ARTIST)
            {
                tex = get_texture_from_artist(pin.artist->id);
                color = CYAN;
            }
            if (pin.type == MPUID_PLAYLIST)
            {
                tex = get_texture_from_playlist(pin.playlist->id);
                color = ORANGE;
            }

            ImGui::SetCursorPos(ImVec2(root_pos.x + padding, root_pos.y + row * row_advance));
            ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, color);
            ImGui::Image(tex.id, SMALL_IMAGE_SIZE, tex.uv0, tex.uv1);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImVec2 cursor_pos = ImGui::GetCursorPos();
            switch (pin.type)
            {
                case MPUID_SONG:
                {
                    if (imgui_aligned_text_button(pin.song->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    {
                        updated_pin_uid = pin.uid;
                        set_center_view_song(mp_get_song(pin.song->id));
                    }
                    break;
                }
                case MPUID_PLAYLIST:
                {
                    if (imgui_aligned_text_button(pin.playlist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    {
                        updated_pin_uid = pin.uid;
                        set_center_view_playlist(mp_get_playlist(pin.playlist->id));
                    }
                    break;
                }
                case MPUID_ALBUM:
                {
                    if (imgui_aligned_text_button(pin.album->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    {
                        updated_pin_uid = pin.uid;
                        set_center_view_album(mp_get_album(pin.album->id));
                    }
                    break;
                }
                case MPUID_ARTIST:
                {
                    if (imgui_aligned_text_button(pin.artist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    {
                        updated_pin_uid = pin.uid;
                        set_center_view_artist(mp_get_artist(pin.artist->id));
                    }
                    break;
                }
                default: break;
            }
            row++;
        };

    for (Pin& pin : mp_ctx.pins)
        list_pin(pin);

    if (updated_pin_uid != INVALID_UID)
        mp_update_pin(updated_pin_uid);
}

static void draw_imgui()
{
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiWindowFlags window_flags{};
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    window_flags |= ImGuiWindowFlags_NoScrollbar;
    window_flags |= ImGuiWindowFlags_NoScrollWithMouse;

    const ImVec2& display_size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);
    static bool window_open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("yamp", &window_open, window_flags);

    static bool player_open = true;
    float player_height = (player_open) ? 128.0f : 0.0f;
    float header_height = 48.0f;
    float interface_height = display_size.y - player_height - header_height;

    const ImVec2 header_size = ImVec2(display_size.x, header_height);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(header_size);
    ImGui::BeginChild("##Header", header_size);
    draw_header(header_size);
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, header_height));
    ImGui::Separator();

    ImGui::SetNextWindowPos(ImVec2(0.0f, header_height));
    ImGui::SetNextWindowSize(ImVec2(display_size.x, interface_height));
    ImGui::BeginChild("##Interface", ImVec2(0.0f, interface_height));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("view", 1 + ctx.quick_access_open + ctx.queue_open, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
    {
        if (ctx.quick_access_open)
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_NoSort);
        if (ctx.queue_open)
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, 300);

        ImGui::TableNextRow();

        if (ctx.quick_access_open)
        {
            ImGui::TableNextColumn();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
            ImGui::BeginChild("##Left Side", ImGui::GetContentRegionAvail());
            draw_pins();
            ImGui::EndChild();
        }

        ImGui::TableNextColumn();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
        ImGui::BeginChild("##Center", ImGui::GetContentRegionAvail());
        draw_center();
        ImGui::EndChild();

        if (ctx.queue_open)
        {
            ImGui::TableNextColumn();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
            ImGui::BeginChild("##Right Side", ImGui::GetContentRegionAvail());
            draw_queue();
            ImGui::EndChild();
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::EndChild();

    if (player_open)
    {
        ImGui::SetCursorPos(ImVec2(0.0f, display_size.y - player_height));
        ImGui::Separator();

        ImGui::SetNextWindowPos(ImVec2(0.0f, display_size.y - player_height));
        ImGui::SetNextWindowSize(ImVec2(display_size.x, player_height));
        const ImVec2 size = ImVec2(display_size.x, player_height);
        ImGui::BeginChild("##Child", size);
        draw_player(size);
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleVar();

    if (ctx.show_demo_window)
        ImGui::ShowDemoWindow(&ctx.show_demo_window);

#ifndef NDEBUG
    if (ctx.show_debug_window)
        draw_debug_info();
#endif

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void window_refresh_callback(GLFWwindow* window)
{
    draw_imgui();
    glfwSwapBuffers(window);
}

void ui_loop()
{
    glfwSetWindowRefreshCallback(ctx.window, window_refresh_callback);
    while (!glfwWindowShouldClose(ctx.window))
    {
        glfwPollEvents();

        double start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        mp_update();
        if (glfwGetWindowAttrib(ctx.window, GLFW_ICONIFIED) != 0)
            continue;

        int display_w, display_h;
        glfwGetFramebufferSize(ctx.window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        draw_imgui();
        double end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        ctx.debug.fps = end - start;

        if (ctx.prompt.has_value() && ctx.prompt->wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready)
        {
            if (ctx.prompt_callback != nullptr)
                ctx.prompt_callback();
            ctx.prompt_callback = nullptr;
            ctx.prompt.reset();
        }

        glfwSwapBuffers(ctx.window);
    }
}

