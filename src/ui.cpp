// *****************************
// Includes
// *****************************

#include "ui.h"
#include "mp.h"
#include "config.h"
#include <cstring>
#include <imgui.h>
#include <unordered_map>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_freetype.h>
#include <portable-file-dialogs.h>
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <queue>
#include <vector>
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

// *****************************
// Global Constants, Variables, Types
// *****************************

#define SEARCH_RESULTS_PER_PAGE 20
#define STRING_LENGTH 512

#define TRANSPARENT ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
#define BLACK ImVec4(0.0f, 0.0f, 0.0f, 1.0f)
#define WHITE ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
#define GRAY ImVec4(0.6f, 0.6f, 0.6f, 0.6f)
#define LIGHT_GRAY ImVec4(0.8f, 0.8f, 0.8f, 0.8f)
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

#define UV0 ImVec2(0.0f, 0.0f)
#define UV1 ImVec2(1.0f, 1.0f)

#define UV0_INVERTED_HORIZONTALLY ImVec2(1.0f, 0.0f)
#define UV1_INVERTED_HORIZONTALLY ImVec2(0.0f, 1.0f)

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
    SHOW_CENTER_SEARCH_RESULT
};

enum SearchResultEnum {
    SEARCH_TYPE_SONG,
    SEARCH_TYPE_ALBUM,
    SEARCH_TYPE_ARTIST,
    SEARCH_TYPE_PLAYLIST
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
        Slot default_album_art_slot;

        GLTexture play_button;
        GLTexture queue_button;
        GLTexture skip_button;
        GLTexture pause_button;
        GLTexture repeat_button;
        GLTexture shuffle_button;
        GLTexture autoplay_button;
        GLTexture show_queue_button;
        GLTexture clear_queue_button;
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
        GLTexture compact;
    } textures;

    struct Debug {
        double fps;
    } debug;

    std::mutex load_queue_lock;
    std::vector<MPUID> load_queue;
    std::mutex unload_queue_lock;
    std::vector<MPUID> unload_queue;

    bool show_demo_window;
    bool show_debug_window;

    bool quick_access_open = true;

    struct {
        ViewEnum type;
        LruCacheRef<Song> song;
        LruCacheRef<Album> album;
        LruCacheRef<Playlist> playlist;
        LruCacheRef<Artist> artist;

        // if type is album or playlist, then write to song_tracks
        // otherwise, write to songs
        std::vector<SongTrack> song_tracks;
        std::vector<LruCacheRef<Song>> songs;
        std::vector<LruCacheRef<Album>> albums;

        struct {
            char song[512];
            char artist[512];
            char album[512];
            char year[32];
        } edit;

        char song_lyrics[MAX_LYRICS_SIZE];

        SearchResultEnum search_result_type;
        SearchResult<Song> song_search_result;
        SearchResult<Album> album_search_result;
        SearchResult<Artist> artist_search_result;
        SearchResult<Playlist> playlist_search_result;
    } center;

    struct {
        ViewEnum type;
        LruCacheRef<Song> song;
        GLuint texture;
        char song_title[STRING_LENGTH];
        char song_artist[STRING_LENGTH];
        char song_album[STRING_LENGTH];
        bool changed;
    } right;

    std::vector<LruCacheRef<Playlist>> playlists;

    std::deque<std::pair<ViewEnum, GenericID>> view_history;

    char search_query[256];

    bool window_maximized;
};

static UIContext ctx;

// *****************************
// ImGUI Helper Functions
// *****************************

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

static void imgui_aligned_text(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align = ALIGN_LEFT, AlignmentEnum v_align = ALIGN_TOP)
{
    ImVec2 text_size = ImGui::CalcTextSize(text);

    if (text_size.x < region_size.x)
    {
        ImGui::SetCursorPos(ImVec2(
                    region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                    region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));
        ImGui::Text("%s", text);
        return;
    }

    constexpr int MAX_STRING_SIZE = 512;
    const char* ellipse = "…";
    ImVec2 ellipse_size = ImGui::CalcTextSize(ellipse);
    int text_end_idx = 0;
    while (text_end_idx < MAX_STRING_SIZE && text[text_end_idx] != '\0' && ImGui::CalcTextSize(text, text + text_end_idx).x < region_size.x - ellipse_size.x)
        text_end_idx++;

    // Dont even draw text lol wtf
    if (text_end_idx == 0)
        return;

    text_size = ImGui::CalcTextSize(text, text + text_end_idx - 1);
    ImGui::SetCursorPos(ImVec2(
                region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));

    char buf[MAX_STRING_SIZE];
    strncpy(buf, text, text_end_idx - 1);
    buf[text_end_idx - 1] = '\0';
    ImGui::Text("%s…", buf);
}

static bool imgui_image_button(ImTextureRef tex_ref, const ImVec2& image_size, const ImVec4& tint_col = WHITE, const ImVec2& uv0 = UV0, const ImVec2& uv1 = UV1)
{
    ImGui::ImageWithBg(tex_ref, image_size, uv0, uv1, BLACK, tint_col);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return ImGui::IsItemClicked();
}

static bool imgui_aligned_text_button(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align = ALIGN_LEFT, AlignmentEnum v_align = ALIGN_TOP)
{
    ImVec2 text_size = ImGui::CalcTextSize(text);

    if (text_size.x < region_size.x)
    {
        ImGui::SetCursorPos(ImVec2(
                    region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                    region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));
        return imgui_text_button(text);
    }

    constexpr int MAX_STRING_SIZE = 512;
    const char* ellipse = "…";
    ImVec2 ellipse_size = ImGui::CalcTextSize(ellipse);
    int text_end_idx = 0;
    while (text_end_idx < MAX_STRING_SIZE && text[text_end_idx] != '\0' && ImGui::CalcTextSize(text, text + text_end_idx).x < region_size.x - ellipse_size.x)
        text_end_idx++;

    // Dont even draw text lol wtf
    if (text_end_idx == 0)
        return false;

    text_size = ImGui::CalcTextSize(text, text + text_end_idx - 1);
    ImGui::SetCursorPos(ImVec2(
                region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));

    char buf[MAX_STRING_SIZE];
    strncpy(buf, text, text_end_idx - 1);
    buf[text_end_idx - 1] = '\0';
    char buf2[MAX_STRING_SIZE];
    snprintf(buf2, sizeof(buf2), "%s…", buf);
    return imgui_text_button(buf2);
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
    ImGui::Separator();
    ImGui::SetCursorPos(origin);
    params.col_offsets.push_back(&params.table_width);
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
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + params.header_row_height + 2.0f));
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

// *****************************
// ImGUI Helper Functions End
// *****************************

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
                static_cast<float>(slot.xoff + LARGE_COVER_ART_SIZE) / TEXTURE_SIZE, 
                static_cast<float>(slot.yoff + LARGE_COVER_ART_SIZE) / TEXTURE_SIZE)
    };
}

static GLTexture get_texture_default()
{
    return get_texture_from_slot(ctx.textures.default_album_art_slot);
}

static GLTexture get_texture_from_song(SongID song_id)
{
    MPUID uid = mp_get_song_uid(song_id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
        return get_texture_from_slot(ctx.textures.slot_map[uid]);
    return get_texture_default();
}

static GLTexture get_texture_from_album(AlbumID album_id)
{
    MPUID uid = mp_get_album_uid(album_id);
    if (ctx.textures.slot_map.find(uid) != ctx.textures.slot_map.end())
        return get_texture_from_slot(ctx.textures.slot_map[uid]);
    return get_texture_default();
}

[[maybe_unused]] static GLTexture get_texture_from_playlist(PlaylistID playlist_id)
{
    (void)playlist_id;
    return get_texture_default();
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

    const int slots_across = TEXTURE_SIZE / LARGE_COVER_ART_SIZE;
    slot.xoff = (slot.idx % SLOTS_PER_TEXTURE) % slots_across * LARGE_COVER_ART_SIZE;
    slot.yoff = (slot.idx % SLOTS_PER_TEXTURE) / slots_across * LARGE_COVER_ART_SIZE;

    return slot;
}

static Slot create_texture(const unsigned char* data)
{
    Slot slot = get_slot();
    glBindTexture(GL_TEXTURE_2D, slot.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot.xoff, slot.yoff, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return slot;
}

static void delete_texture(Slot& slot)
{
    ctx.textures.free_slot_textures.push(slot.idx);
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

// these have to be after the objects are moved bc those objects
// are often from these containers
static void reset_center_view_containers()
{
    ctx.center.song_tracks.clear();
    ctx.center.songs.clear();
    ctx.center.albums.clear();
    ctx.center.song_lyrics[0] = '\0';
    ctx.center.edit.song[0] = '\0';
    ctx.center.edit.artist[0] = '\0';
    ctx.center.edit.album[0] = '\0';
    ctx.center.edit.year[0] = '\0';
}

static void set_center_view_lyrics(const LruCacheRef<Song>& song)
{
    (void)song;
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

static void set_center_view_edit()
{
    ctx.center.type = SHOW_CENTER_EDIT;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    ctx.center.song = nullptr;
    reset_center_view_containers();
}

static void set_center_view_song(LruCacheRef<Song>&& song)
{
    ctx.center.type = SHOW_CENTER_SONG;
    ctx.center.artist = mp_get_artist_from_song(song->id);
    ctx.center.playlist = nullptr;
    ctx.center.album = mp_get_album_from_song(song->id);
    ctx.center.song = std::move(song);
    reset_center_view_containers();

    mp_get_song_lyrics(song->id, ctx.center.song_lyrics, sizeof(ctx.center.song_lyrics));
    snprintf(ctx.center.edit.song, sizeof(ctx.center.edit.song), "%s", ctx.center.song->title.c_str());
    if (ctx.center.artist != nullptr)
        snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
    if (ctx.center.album != nullptr)
        snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
    snprintf(ctx.center.edit.year, sizeof(ctx.center.edit.year), "%d", ctx.center.song->year);

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

    std::vector<SongTrackID> tracks = mp_get_songs_from_album(ctx.center.album->id);
    for (auto & [song_id, track] : tracks)
        ctx.center.song_tracks.emplace_back(mp_get_song(song_id), track);

    snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
    if (ctx.center.artist != nullptr)
        snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());
    snprintf(ctx.center.edit.year, sizeof(ctx.center.edit.year), "%d", ctx.center.album->year);

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

    std::vector<SongTrackID> tracks = mp_get_songs_from_playlist(ctx.center.playlist->id);
    for (auto & [song_id, track] : tracks)
        ctx.center.song_tracks.emplace_back(mp_get_song(song_id), track);

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

    std::vector<AlbumID> albums = mp_get_albums_from_artist(ctx.center.artist->id);
    for (AlbumID album_id : albums)
        ctx.center.albums.emplace_back(mp_get_album(album_id));

    ctx.center.song_search_result = mp_get_paginated_songs_from_artist(ctx.center.artist->id, SEARCH_RESULTS_PER_PAGE, 1);
    snprintf(ctx.center.edit.artist, sizeof(ctx.center.edit.artist), "%s", ctx.center.artist->name.c_str());

    push_history_entry(SHOW_CENTER_ARTIST, ctx.center.artist->id);
}

static void set_center_view_search_result()
{
    ctx.center.type = SHOW_CENTER_SEARCH_RESULT;
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
    ctx.right.type = (ctx.right.type == SHOW_RIGHT_QUEUE) ? SHOW_RIGHT_NONE : SHOW_RIGHT_QUEUE;
    ctx.right.song = nullptr;
}

static void initialize_default_texture(GLuint* id, const char* path)
{
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int nc;
    FILE* fptr = fopen(path, "r");
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
                NULL, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0,
                STBIR_4CHANNEL);
    ctx.textures.default_album_art_slot = create_texture(resized_data);
    stbi_image_free(data);
    free(resized_data);
}

static void initialize_default_textures()
{
    initialize_default_texture_slot("assets/icons/No-album-art.png");
    initialize_default_texture(&ctx.textures.play_button.id, "assets/icons/play.png");
    initialize_default_texture(&ctx.textures.queue_button.id, "assets/icons/add-to-playlist.png");
    initialize_default_texture(&ctx.textures.skip_button.id, "assets/icons/skip.png");
    initialize_default_texture(&ctx.textures.pause_button.id, "assets/icons/pause.png");
    initialize_default_texture(&ctx.textures.repeat_button.id, "assets/icons/repeat.png");
    initialize_default_texture(&ctx.textures.shuffle_button.id, "assets/icons/shuffle.png");
    initialize_default_texture(&ctx.textures.autoplay_button.id, "assets/icons/autoplay.png");
    initialize_default_texture(&ctx.textures.volume.id, "assets/icons/volume-up.png");
    initialize_default_texture(&ctx.textures.mute.id, "assets/icons/mute.png");
    initialize_default_texture(&ctx.textures.show_queue_button.id, "assets/icons/show-queue.png");
    initialize_default_texture(&ctx.textures.clear_queue_button.id, "assets/icons/clear-queue.png");
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
    initialize_default_texture(&ctx.textures.compact.id, "assets/icons/minimize-arrows.png");
    glGenTextures(1, &ctx.right.texture);
}

static void cleanup_textures()
{
    glDeleteTextures(1, &ctx.right.texture);
}

[[maybe_unused]] static void song_constructor_callback(Song* song)
{
    (void)song;
}

[[maybe_unused]] static bool loading_textures()
{
    return ctx.load_queue.size() > 0;
}

[[maybe_unused]] static bool is_song_texture_loading(int song_id)
{
    return ctx.textures.slot_map.find(song_id) == ctx.textures.slot_map.end();
}

static void update_textures()
{
    if (ctx.load_queue.size() > 0)
    {
        std::lock_guard lock_guard{ctx.load_queue_lock};

        double start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        for (MPUID uid : ctx.load_queue)
        {
            Art art = mp_get_art(uid);
            if (art.data == nullptr)
                ctx.textures.slot_map[uid] = ctx.textures.default_album_art_slot;
            else
                ctx.textures.slot_map[uid] = create_texture(art.data);
            mp_free_art(&art);
        }
        double end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        SPDLOG_INFO("Updated textures in {} ms", (end - start) / 1000);

        ctx.load_queue.clear();
    }

    if (ctx.unload_queue.size() > 0)
    {
        std::lock_guard lock_guard{ctx.load_queue_lock};

        // if more songs are add than removed, then number of
        // texture slots used might blow up. otherwise, you could
        // get away with unloading one texture every frame
        for (MPUID uid : ctx.unload_queue)
        {
            if (ctx.textures.slot_map[uid].idx != ctx.textures.default_album_art_slot.idx)
                delete_texture(ctx.textures.slot_map[uid]);
            ctx.textures.slot_map.erase(ctx.textures.slot_map.find(uid));
        }
        ctx.unload_queue.clear();

        //MPUID uid = ctx.unload_queue.front();
        //auto it = ctx.textures.slot_map.find(uid); 
        //if (it == ctx.textures.slot_map.end())
        //{
        //    std::swap(ctx.unload_queue.front(), ctx.unload_queue.back());
        //}
        //else
        //{
        //    delete_texture(ctx.textures.slot_map[uid]);
        //    ctx.textures.slot_map.erase(it);
        //    std::swap(ctx.unload_queue.front(), ctx.unload_queue.back());
        //    ctx.unload_queue.pop_back();
        //}
    }
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
    //pfd::settings::verbose(true);
    IMGUI_CHECKVERSION();

    //const char* font_file_path = "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf";
    //const char* font_file_path = "/usr/share/fonts/truetype/noto/NotoSansAdlam-Regular.ttf";
    //const char* font_file_path = "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
    const char* font_file_path = "assets/fonts/Inter,Noto_Color_Emoji/Inter/Inter-VariableFont_opsz,wght.ttf";
    //const char* emoji_font_file_path = "assets/fonts/Inter,Noto_Color_Emoji/Noto_Color_Emoji/NotoColorEmoji-Regular.ttf";

    // make everything 0 so i dont deal with weird default formatting issues
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 18.0f;
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.WindowBorderSize = 0.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.0f);
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.FramePadding = ImVec2(0.0f, 0.0f);
    //style.ItemSpacing = ImVec2(0.0f, 0.0f);
    style.ItemInnerSpacing = ImVec2(0.0f, 0.0f);
    style.CellPadding = ImVec2(0.0f, 0.0f);
    style.TabRounding = 0.0f;

    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    style.WindowBorderSize = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    const ImWchar icons_ranges[] = { 0xf000, 0xf3ff, 0 };
    io.Fonts->AddFontFromFileTTF(font_file_path, style.FontSizeBase, nullptr, icons_ranges);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);

    mp_ctx.songs.set_constructor_callback(
        [](Song* song) -> void
        {
            SPDLOG_INFO("Loading Song {} {}", song->id, song->title);
            std::lock_guard lock{ctx.load_queue_lock};
            ctx.load_queue.push_back(mp_get_song_uid(song->id));
        });

    mp_ctx.songs.set_destructor_callback(
        [](Song* song) -> void
        {
            SPDLOG_INFO("Unloading Song {} {}", song->id, song->title);
            std::lock_guard lock{ctx.unload_queue_lock};
            ctx.unload_queue.push_back(mp_get_song_uid(song->id));
        });

    mp_ctx.albums.set_constructor_callback(
        [](Album* album) -> void
        {
            SPDLOG_INFO("Loading Album {} {}", album->id, album->name);
            std::lock_guard lock{ctx.load_queue_lock};
            ctx.load_queue.push_back(mp_get_album_uid(album->id));
        });

    mp_ctx.albums.set_destructor_callback(
        [](Album* album) -> void
        {
            SPDLOG_INFO("Unloading Album {} {}", album->id, album->name);
            std::lock_guard lock{ctx.unload_queue_lock};
            ctx.unload_queue.push_back(mp_get_album_uid(album->id));
        });

    //ctx.playlists = mp_get_playlists();
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
    ctx.playlists.clear();
}

void ui_cleanup()
{
    cleanup_textures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();

    destroy_cache_references();

    SPDLOG_INFO("UI cleaned up");
}

struct DrawSongTableParams {
    std::vector<SongTrack>* song_tracks;
    std::vector<LruCacheRef<Song>>* songs;
    std::vector<float*> column_offsets;
    bool use_song_tracks = false;
    bool show_track = false;
    bool show_art = true;
    bool show_title = true;
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
                                 + params.show_title
                                 + params.show_album
                                 + params.show_artist
                                 + params.show_length;
    assert(static_cast<size_t>(custom_table_params.num_cols) == params.column_offsets.size());
    assert(!(params.use_song_tracks && !params.show_track));
    assert(!(params.use_song_tracks && params.song_tracks == nullptr));

    if (params.use_song_tracks)
        custom_table_params.num_rows = static_cast<int>(params.song_tracks->size());
    else
        custom_table_params.num_rows = static_cast<int>(params.songs->size());

    custom_table_params.header_row_height = 40.0f;
    custom_table_params.row_height = 48.0f;
    custom_table_params.table_width = ImGui::GetContentRegionAvail().x;

    //static float title_col_offset = 10.0f;
    //static float artist_col_offset = 300.0f;
    //static float album_col_offset = 550.0f;
    //static float length_col_right_offset = 100.0f;
    //float length_col_offset = custom_table_params.table_width - length_col_right_offset;

    static float title_col_width = 300.0f;
    static float artist_col_width = 300.0f;
    static float album_col_width = 300.0f;
    static float length_col_width = 50.0f;

    static int selected_row = -1;

    custom_table_params.selected_row = &selected_row;
    custom_table_params.menu_popup_callback = [&params](int* selected_row) -> MenuPopupEnum
        {
                LruCacheRef<Song>& song = (params.use_song_tracks)
                                    ? params.song_tracks->at(*selected_row).song
                                    : params.songs->at(*selected_row);
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
            if (ImGui::Button("Add to Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const LruCacheRef<Playlist>& playlist : ctx.playlists)
                {
                    ImGui::PushID(playlist->id);
                    if (ImGui::Button(playlist->name.c_str()))
                        mp_add_song_to_playlist(song->id, playlist->id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    LruCacheRef<Playlist> playlist = mp_create_playlist();
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    mp_add_song_to_playlist(song->id, playlist->id);
                    set_center_view_playlist(std::move(playlist));
                    ImGui::EndPopup();
                    return MENU_CLOSE_TABLE;
                }
                ImGui::EndPopup();
            }
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
                //imgui_aligned_text(std::to_string(tracks[row].track).c_str(), region_start, region_size, ALIGN_CENTER, ALIGN_CENTER);
                auto& [song, track] = params.song_tracks->at(row);
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
                    const ImVec4 bg_col = TRANSPARENT;
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

    if (params.show_title)
    {
        custom_table_params.col_widths.push_back(title_col_width);
        custom_table_params.header_row_col_callback.push_back(
            [](ImVec2 region_start, ImVec2 region_size)
            {
                imgui_aligned_text("Title", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
            });
        custom_table_params.body_row_col_callback.push_back(
            [&center_song, &params](ImVec2 region_start, ImVec2 region_size, int row)
            {
                LruCacheRef<Song>& song = (params.use_song_tracks)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row);

                ImVec2 picture_size{};
                ImGui::SetCursorPos(region_start);
                if (params.show_art)
                {
                    picture_size = ImVec2{48.0f, 48.0f};
                    const bool hovered = imgui_is_region_hovered(ImGui::GetCursorScreenPos(), picture_size);
                    GLTexture tex = get_texture_from_song(song->id);
                    ImVec4 tint = (hovered) ? GRAY : WHITE;
                    ImGui::ImageWithBg(tex.id, picture_size, tex.uv0, tex.uv1, TRANSPARENT, tint);
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
                const float padding = 5.0f;
                bool pressed = imgui_aligned_text_button(song->title.c_str(), ImVec2(region_start.x + picture_size.x + padding, region_start.y), ImVec2(region_size.x - picture_size.x - padding, region_size.y), ALIGN_LEFT, ALIGN_CENTER);
                if (pressed)
                    center_song = std::move(song);
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
                LruCacheRef<Song>& song = (params.use_song_tracks)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row);

                LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
                if (artist == nullptr)
                    return;
                if (imgui_aligned_text_button(artist->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                    center_artist = std::move(artist);
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
                LruCacheRef<Song>& song = (params.use_song_tracks)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row);

                LruCacheRef<Album> album = mp_get_album_from_song(song->id);
                if (album == nullptr)
                    return;
                if (imgui_aligned_text_button(album->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                    center_album = std::move(album);
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
                LruCacheRef<Song>& song = (params.use_song_tracks)
                                    ? params.song_tracks->at(row).song
                                    : params.songs->at(row);

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

static void draw_album_table(std::vector<LruCacheRef<Album>>& albums, bool compact)
{
    LruCacheRef<Album> center_album = nullptr;

    ImGui::BeginChild("album_table", ImGui::GetContentRegionAvail());
    if (compact)
    {
        float padding = 10.0f;
        ImVec2 album_art_size = ImVec2(180.0f, 180.0f);
        float row_width = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        ImVec2 origin = ImGui::GetCursorPos();
        ImGui::Dummy({0.0f, 0.0f});

        for (LruCacheRef<Album>& album : albums)
        {
            ImGui::PushID(album->id);
            ImGui::SetCursorPos(ImVec2(origin.x, origin.y));
            GLTexture tex = get_texture_from_album(album->id);
            if (imgui_image_button(tex.id, album_art_size, WHITE, tex.uv0, tex.uv1))
            {
                center_album = std::move(album);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();

            origin.x += album_art_size.x + padding;
            if (origin.x + album_art_size.x + padding > row_width)
            {
                origin.x = padding;
                origin.y += album_art_size.y + 40.0f;
            }
        }
    }
    else
    {
        ImVec2 region_start = ImGui::GetCursorPos();
        region_start.x += 10.0f;
        region_start.y += 10.0f;
        ImVec2 region_size = ImGui::GetContentRegionAvail();
        ImGui::PushFont(NULL, 32.0f);
        ImVec2 image_size = ImVec2(64, 64);
        float spacing = 10.0f;
        for (LruCacheRef<Album>& album : albums)
        {
            ImGui::PushID(album->id);
            GLTexture tex = get_texture_from_album(album->id);
            float text_height = ImGui::CalcTextSize(album->name.c_str()).y;
            ImGui::SetCursorPos(region_start);
            imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
            if (imgui_aligned_text_button(album->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
            {
                center_album = std::move(album);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
            region_start.y += image_size.y + spacing;
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();

    if (center_album != nullptr)
        set_center_view_album(std::move(center_album));
}

static void draw_search_results()
{
    const float padding = 10.0f;
    ImGui::SetCursorPos(ImVec2(padding, padding));

    static int current_search_type = static_cast<int>(ctx.center.search_result_type);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 0.0f));
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
    ImGui::PopStyleVar();

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

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);

    if (current_search_type == SEARCH_TYPE_SONG)
    {
        static float title_col_offset = 10.0f;
        static float artist_col_offset = 300.0f;
        static float album_col_offset = 550.0f;
        static float length_col_right_offset = 100.0f;

        DrawSongTableParams params{};
        params.songs = &ctx.center.song_search_result.entries;
        params.column_offsets = {&title_col_offset, &artist_col_offset, &album_col_offset, &length_col_right_offset};
        draw_song_table(params);
    }
    else if (current_search_type == SEARCH_TYPE_ALBUM)
    {
        std::vector<LruCacheRef<Album>> albums{};
        ImGui::Separator();
        draw_album_table(ctx.center.album_search_result.entries, false);
    }
    else if (current_search_type == SEARCH_TYPE_ARTIST)
    {
        ImGui::Separator();
        ImGui::BeginChild("artist_table", ImGui::GetContentRegionAvail());
        ImVec2 region_start = ImGui::GetCursorPos();
        region_start.x += 10.0f;
        region_start.y += 10.0f;
        ImVec2 region_size = ImGui::GetContentRegionAvail();
        ImGui::PushFont(NULL, 32.0f);
        ImVec2 image_size = ImVec2(64, 64);
        float spacing = 10.0f;
        for (LruCacheRef<Artist>& artist : ctx.center.artist_search_result.entries)
        {
            ImGui::PushID(artist->id);
            GLTexture tex = get_texture_default();
            float text_height = ImGui::CalcTextSize(artist->name.c_str()).y;
            ImGui::SetCursorPos(region_start);
            imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
            if (imgui_aligned_text_button(artist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
            {
                set_center_view_artist(std::move(artist));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
            region_start.y += image_size.y + spacing;
        }
        ImGui::PopFont();
        ImGui::EndChild();
    }
    else if (current_search_type == SEARCH_TYPE_PLAYLIST)
    {
        ImGui::Separator();
        ImGui::BeginChild("playlist_table", ImGui::GetContentRegionAvail());
        ImVec2 region_start = ImGui::GetCursorPos();
        region_start.x += 10.0f;
        region_start.y += 10.0f;
        ImVec2 region_size = ImGui::GetContentRegionAvail();
        ImGui::PushFont(NULL, 32.0f);
        ImVec2 image_size = ImVec2(64, 64);
        float spacing = 10.0f;
        for (LruCacheRef<Playlist>& playlist : ctx.center.playlist_search_result.entries)
        {
            ImGui::PushID(playlist->id);
            GLTexture tex = get_texture_default();
            float text_height = ImGui::CalcTextSize(playlist->name.c_str()).y;
            ImGui::SetCursorPos(region_start);
            imgui_image_button(tex.id, image_size, WHITE, tex.uv0, tex.uv1);
            if (imgui_aligned_text_button(playlist->name.c_str(), ImVec2(region_start.x + image_size.x + 5.0f, region_start.y + (image_size.y - text_height) / 2.0f), region_size))
            {
                set_center_view_playlist(std::move(playlist));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
            region_start.y += image_size.y + spacing;
        }
        ImGui::PopFont();
        ImGui::EndChild();
    }
}

static void draw_song_info()
{
    assert(ctx.center.song != nullptr);

    LruCacheRef<Song>& song = ctx.center.song;
    LruCacheRef<Artist>& artist = ctx.center.artist;
    LruCacheRef<Album>& album = ctx.center.album;

    ImVec2 origin = ImGui::GetCursorPos();

    enum {
        SHOW_NONE,
        SHOW_LYRICS,
        SHOW_EDIT,
        SHOW_ALBUM_PICKER
    };

    static int show = SHOW_NONE;

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);
    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_song(song->id);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();
    {
        ImVec2 region_start(
                origin.x + album_art_size.x + 2 * album_art_padding,
                origin.y + album_art_padding);
        ImGui::SetCursorPos(region_start);
        ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);
        ImGui::BeginChild("song_view", region_size);
        region_start = ImGui::GetCursorPos();
        ImGui::SetWindowFontScale(2.0f); 
        imgui_aligned_text(song->title.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        ImGui::SetWindowFontScale(1.5f); 
        if (artist) {
            if (imgui_aligned_text_button(artist->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
                set_center_view_artist(std::move(artist));
                ImGui::EndChild();
                return;
            }
        } else {
            imgui_aligned_text("<No Artist>", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        }
        if (album) {
            if (imgui_aligned_text_button(album->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
                set_center_view_album(std::move(album));
                ImGui::EndChild();
                return;
            }
        } else {
            imgui_aligned_text("<No Album>", ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
        }
        int length = static_cast<int>(ctx.center.song->length);
        if (song->year != INVALID_YEAR)
            ImGui::Text("%u • %d:%02d", album->year, length / 60, length % 60);
        else
            ImGui::Text("%d:%02d", length / 60, length % 60);

        ImGui::SetWindowFontScale(1.0f); 
        ImVec2 button_size(32.0f, 32.0f);
        ImGui::SetCursorPosY(region_start.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);

        GLTexture tex = ctx.textures.play_button;
        if (imgui_image_button(tex.id, button_size))
            mp_play_song(song->id);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
        ImGui::SameLine();
        tex = ctx.textures.queue_button;
        if (imgui_image_button(tex.id, button_size))
            mp_queue_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pin_button.id, button_size))
            mp_pin_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.lyrics_button.id, button_size))
            show = SHOW_LYRICS;

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pencil_button.id, button_size))
            show = SHOW_EDIT;

        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    ImGui::BeginChild("more_song_info", ImGui::GetContentRegionAvail());
    if (show == SHOW_LYRICS)
    {
        static bool editing;
        static bool edited;
        editing ^= ImGui::Button("Edit");
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            edited |= ctx.center.song_lyrics[0] != '\0';
            ctx.center.song_lyrics[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            mp_get_song_lyrics(song->id, ctx.center.song_lyrics, sizeof(ctx.center.song_lyrics));
            edited = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            mp_update_song_lyrics(ctx.center.song->id, ctx.center.song_lyrics);
            edited = false;
            editing = false;
        }
        if (edited)
        {
            ImGui::SameLine();
            ImGui::Text("(Unsaved Changes)");
        }

        ImGui::BeginChild("lyrics", ImGui::GetContentRegionAvail());
        if (editing)
        {
            ImGui::PushFont(NULL, 24.0f);
            edited |= ImGui::InputTextMultiline(
                    "##textarea",
                    ctx.center.song_lyrics,
                    sizeof(ctx.center.song_lyrics),
                    ImVec2(-1.0f, -1.0f)
                );
            ImGui::PopFont();
        }
        else
        {
            const ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
            ImGui::PushFont(NULL, 24.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + frame_padding.x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + frame_padding.y);
            if (ctx.center.song_lyrics[0] == '\0')
                ImGui::Text("No Lyrics");
            else
                ImGui::TextWrapped("%s", ctx.center.song_lyrics);
            ImGui::PopFont();
        }
        ImGui::EndChild();
    }
    else if (show == SHOW_EDIT)
    {
        static bool edited;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        float col_1_cursor_x = 10.0f;
        float col_2_cursor_x = 120.0f;
        ImGui::SetWindowFontScale(1.5f); 
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 8.0f));
        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Song");
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        edited |= ImGui::InputText("##Song", ctx.center.edit.song, sizeof(ctx.center.edit.song));

        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Album");
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        if (imgui_aligned_text_button(ctx.center.edit.album, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()))
        {
            show = SHOW_ALBUM_PICKER;
            ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        }

        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Artist");
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        ImGui::Text("%s", ctx.center.edit.artist);

        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Year");
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        ImGui::Text("%s", ctx.center.edit.year);

        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Path");
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        ImGui::Text("%s", ctx.center.song->path.c_str());

        GLTexture tex = ctx.textures.volume;
        ImVec2 volume_slider_size = ImVec2(150.0f, 5.0f);
        ImVec2 volume_icon_size = ImVec2(20.0f, 20.0f);
        ImGui::SetCursorPosX(col_1_cursor_x);
        ImGui::Text("Volume");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::SameLine();
        ImGui::SetCursorPosX(col_2_cursor_x);
        ImGui::Image(tex.id, volume_icon_size);
        ImGui::PopStyleVar();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (volume_icon_size.y - volume_slider_size.y) / 2.0f);
        if (imgui_custom_slider_float("##Volume", &ctx.center.song->volume, 0.0f, 2.0f, volume_slider_size))
        {
            mp_update_volume();
            edited = true;
        }

        ImGui::SetCursorPosX(col_1_cursor_x);
        if (ImGui::Button("Save"))
        {
            mp_update_song(ctx.center.song->id, ctx.center.edit.song);
            mp_update_song_volume(ctx.center.song->id, ctx.center.song->volume);
            edited = false;
        }
        if (edited)
        {
            ImGui::SameLine();
            ImGui::Text(" [Unsaved Changes]");
        }

        ImGui::SetCursorPosX(col_1_cursor_x);
        if (ImGui::Button("Update Metadata"))
            ;

        ImGui::PopStyleVar();
        ImGui::SetWindowFontScale(1.0f); 
    }
    else if (show == SHOW_ALBUM_PICKER)
    {
        const float padding = 10.0f;
        int page_num = ctx.center.album_search_result.page_num;
        int num_pages = ctx.center.album_search_result.num_pages;
        int num_results = ctx.center.album_search_result.num_results;

        ImGui::SetCursorPos(ImVec2(padding, padding));
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
        ImGui::PushFont(NULL, 32.0f);
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
                mp_switch_song_album(song->id, test_album->id);
                ctx.center.album = std::move(test_album);
                snprintf(ctx.center.edit.album, sizeof(ctx.center.edit.album), "%s", ctx.center.album->name.c_str());
                show = SHOW_EDIT;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
            region_start.y += image_size.y + spacing;
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

static void draw_album_info()
{
    assert(ctx.center.album != nullptr);

    std::vector<SongTrack>& tracks = ctx.center.song_tracks;

    LruCacheRef<Album>& album = ctx.center.album;
    LruCacheRef<Artist>& artist = ctx.center.artist;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);
    
    GLTexture tex = get_texture_from_album(ctx.center.album->id);
    ImGui::ImageWithBg(tex.id, album_art_size, tex.uv0, tex.uv1, BLACK);

    static bool show_edit;

    ImGui::SameLine();
    {
        ImVec2 region_start(
                origin.x + album_art_size.x + 2 * album_art_padding,
                origin.y + album_art_padding);
        ImGui::SetCursorPos(region_start);
        ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

        ImGui::BeginChild("album_view", region_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        region_start = ImGui::GetCursorPos();
        ImGui::SetWindowFontScale(2.0f); 
        imgui_aligned_text(album->name.c_str(), region_start, region_size);
        ImGui::SetWindowFontScale(1.5f); 
        if (artist) {
            if (imgui_aligned_text_button(artist->name.c_str(), ImGui::GetCursorPos(), ImGui::GetContentRegionAvail())) {
                set_center_view_artist(std::move(artist));
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

        ImGui::SetWindowFontScale(1.0f); 
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
        if (imgui_image_button(ctx.textures.queue_button.id, button_size))
            for (const auto& [song, track] : tracks)
                mp_queue_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pin_button.id, button_size))
            mp_pin_album(album->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pencil_button.id, ImVec2(32, 32)))
            show_edit = !show_edit;

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.menu_button.id, button_size))
            ImGui::OpenPopup("add_to_playlist_popup");
        if (ImGui::BeginPopup("add_to_playlist_popup"))
        {
            for (const LruCacheRef<Playlist>& playlist : ctx.playlists)
            {
                ImGui::PushID(playlist->id);
                if (ImGui::Button(playlist->name.c_str()))
                    mp_add_album_to_playlist(album->id, playlist->id);
                ImGui::PopID();
            }
            if (ImGui::Button("Create Playlist"))
            {
                LruCacheRef<Playlist> playlist = mp_create_playlist();
                mp_add_album_to_playlist(album->id, playlist->id);
                set_center_view_playlist(std::move(playlist));
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));

    if (show_edit)
    {
        ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
        ImGui::Separator();
        ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
        ImGui::BeginChild("edited", ImGui::GetContentRegionAvail());

        static bool edited;

        if (ImGui::Button("Save"))
        {
            mp_update_album(ctx.center.album->id, ctx.center.edit.album);
            edited = false;
        }
        ImGui::SameLine();
        ImGui::Text("Info%s", (edited) ? " [Unsaved Changes]" : "");

        ImGui::Text("Album");
        ImGui::SameLine();
        edited |= ImGui::InputText("##Album", ctx.center.edit.album, sizeof(ctx.center.edit.album));
        ImGui::Text("Artist - ");
        ImGui::SameLine();
        if (ImGui::Button(ctx.center.edit.artist))
            ImGui::OpenPopup("artist_picker");
        ImGui::Text("Year - %s", ctx.center.edit.year);

        if (ImGui::BeginPopup("artist_picker"))
        {
            ImGui::Text("!!!!");
            ImGui::EndPopup();
        }

        ImGui::EndChild();
    }
    else
    {
        static float track_col_offset = 10.0f;
        static float title_col_offset = 70.0f;
        static float length_col_right_offset = 100.0f;

        DrawSongTableParams params{};
        params.show_track = true;
        params.show_album = false;
        params.show_artist = false;
        params.show_art = false;
        params.use_song_tracks = true;
        params.column_offsets = {&track_col_offset, &title_col_offset, &length_col_right_offset};
        params.song_tracks = &ctx.center.song_tracks;

        draw_song_table(params);
    }
}

static void draw_playlist_info()
{
    assert(ctx.center.playlist != nullptr);

    LruCacheRef<Playlist>& playlist = ctx.center.playlist;
    std::vector<SongTrack>& tracks = ctx.center.song_tracks;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);

    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_slot(ctx.textures.default_album_art_slot);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();
    {
        ImVec2 region_start(
                origin.x + album_art_size.x + 2 * album_art_padding,
                origin.y + album_art_padding);
        ImGui::SetCursorPos(region_start);
        ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

        ImGui::BeginChild("playlist_view", region_size);
        region_start = ImGui::GetCursorPos();
        ImGui::SetWindowFontScale(2.0f); 
        imgui_aligned_text(playlist->name.c_str(), region_start, region_size);

        ImGui::SetWindowFontScale(1.5f); 
        int length = static_cast<int>(playlist->length);
        ImGui::Text("%d:%02d", length / 60, length % 60);
        ImGui::SetWindowFontScale(1.0f); 

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
        if (imgui_image_button(ctx.textures.queue_button.id, button_size))
            for (const auto& [song, track] : tracks)
                mp_queue_song(song->id);

        ImGui::SameLine();
        if (imgui_image_button(ctx.textures.pin_button.id, button_size))
            mp_pin_playlist(playlist->id);

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));

    if (tracks.size() == 0) {
        ImGui::Text("No Songs");
        return;
    }

    LruCacheRef<Song> center_song = nullptr;
    LruCacheRef<Artist> center_artist = nullptr;
    LruCacheRef<Album> center_album = nullptr;

    CustomTableParams params{};
    params.num_cols = 5;
    params.num_rows = tracks.size();
    params.header_row_height = 40.0f;
    params.row_height = 48.0f;
    params.table_width = ImGui::GetContentRegionAvail().x;

    static float track_col_offset = 10.0f;
    static float title_col_offset = 70.0f;
    static float artist_col_offset = 300.0f;
    static float album_col_offset = 550.0f;
    static float length_col_right_offset = 100.0f;
    float length_col_offset = params.table_width - length_col_right_offset;

    static float track_col_width = 50.0f;
    static float title_col_width = 300.0f;
    static float artist_col_width = 300.0f;
    static float album_col_width = 300.0f;
    static float length_col_width = 50.0f;

    static int selected_row = -1;

    params.selected_row = &selected_row;
    params.menu_popup_callback = [](int* selected_row) -> MenuPopupEnum
        {
            LruCacheRef<Song>& song = tracks[*selected_row].song;
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
            if (ImGui::Button("Add to Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const LruCacheRef<Playlist>& playlist : ctx.playlists)
                {
                    ImGui::PushID(playlist->id);
                    if (ImGui::Button(playlist->name.c_str()))
                        mp_add_song_to_playlist(song->id, playlist->id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    LruCacheRef<Playlist> playlist = mp_create_playlist();
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    mp_add_song_to_playlist(song->id, playlist->id);
                    set_center_view_playlist(std::move(playlist));
                    ImGui::EndPopup();
                    return MENU_CLOSE_TABLE;
                }
                ImGui::EndPopup();
            }
            if (ImGui::Button("Close"))
                return MENU_CLOSE_POPUP;

            return MENU_END_POPUP;
        };

    params.col_offsets.push_back(&track_col_offset);
    params.col_widths.push_back(track_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("#", region_start, region_size, ALIGN_CENTER, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size, int row)
        {
            //imgui_aligned_text(std::to_string(tracks[row].track).c_str(), region_start, region_size, ALIGN_CENTER, ALIGN_CENTER);
            auto& [song, track] = tracks[row];
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 play_button_size(18.0f, 18.0f);
            ImGui::SetCursorPos(ImVec2(region_start.x + (region_size.x - play_button_size.x) / 2.0f, region_start.y + (region_size.y - play_button_size.y) / 2.0f));
            ImVec2 screen_pos = ImGui::GetCursorScreenPos();
            bool hovered = mouse_pos.x >= screen_pos.x 
                && mouse_pos.x <= screen_pos.x + play_button_size.x
                && mouse_pos.y >= screen_pos.y 
                && mouse_pos.y <= screen_pos.y + play_button_size.y;
            if (!hovered)
            {
                std::string track_str = std::to_string(track);
                ImVec2 text_size = ImGui::CalcTextSize(track_str.c_str());
                ImGui::SetCursorPos(ImVec2(region_start.x + (region_size.x - text_size.x) / 2.0f, region_start.y + (region_size.y - text_size.y) / 2.0f));
                ImGui::Text("%s", track_str.c_str());
            }
            else
            {
                GLTexture tex = ctx.textures.play_button;
                ImGui::PushStyleColor(ImGuiCol_Button, BLACK);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BLACK);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, BLACK);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                const ImVec4 bg_col = TRANSPARENT;
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

    params.col_offsets.push_back(&title_col_offset);
    params.col_widths.push_back(title_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Title", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [&center_song](ImVec2 region_start, ImVec2 region_size, int row)
        {
            auto& [song, track] = tracks[row];
            ImVec2 picture_size(48, 48);
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::SetCursorPos(ImVec2(region_start.x, region_start.y));
            ImGui::Image(tex.id, picture_size, tex.uv0, tex.uv1);
            float padding = 5.0f;
            bool pressed = imgui_aligned_text_button(tracks[row].song->title.c_str(), ImVec2(region_start.x + picture_size.x + padding, region_start.y), ImVec2(region_size.x - picture_size.x - padding, region_size.y), ALIGN_LEFT, ALIGN_CENTER);
            if (pressed)
                center_song = std::move(song);
        });

    params.col_offsets.push_back(&artist_col_offset);
    params.col_widths.push_back(artist_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Artist", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [&center_artist](ImVec2 region_start, ImVec2 region_size, int row)
        {
            auto& [song, track] = tracks[row];
            LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
            if (imgui_aligned_text_button(artist->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                center_artist = std::move(artist);
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
            auto& [song, track] = tracks[row];
            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (imgui_aligned_text_button(album->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                center_album = std::move(album);
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
            LruCacheRef<Song>& song = tracks[row].song;
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

    static float title_col_offset = 10.0f;
    static float album_col_offset = 550.0f;
    static float length_col_right_offset = 100.0f;
    float length_col_offset = params.table_width - length_col_right_offset;

    static float title_col_width = 300.0f;
    static float album_col_width = 300.0f;
    static float length_col_width = 50.0f;

    static int selected_row = -1;

    params.selected_row = &selected_row;
    params.menu_popup_callback = [](int* selected_row) -> MenuPopupEnum
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[*selected_row];
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
            if (ImGui::Button("Add to Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const LruCacheRef<Playlist>& playlist : ctx.playlists)
                {
                    ImGui::PushID(playlist->id);
                    if (ImGui::Button(playlist->name.c_str()))
                        mp_add_song_to_playlist(song->id, playlist->id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    LruCacheRef<Playlist> playlist = mp_create_playlist();
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    mp_add_song_to_playlist(song->id, playlist->id);
                    set_center_view_playlist(std::move(playlist));
                    ImGui::EndPopup();
                    return MENU_CLOSE_TABLE;
                }
                ImGui::EndPopup();
            }
            if (ImGui::Button("Close"))
                return MENU_CLOSE_POPUP;

            return MENU_END_POPUP;
        };

    params.col_offsets.push_back(&title_col_offset);
    params.col_widths.push_back(title_col_width);
    params.header_row_col_callback.push_back(
        [](ImVec2 region_start, ImVec2 region_size)
        {
            imgui_aligned_text("Title", region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
        });
    params.body_row_col_callback.push_back(
        [&center_song](ImVec2 region_start, ImVec2 region_size, int row)
        {
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row];
            ImVec2 picture_size(48, 48);
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::SetCursorPos(region_start);
            const bool hovered = imgui_is_region_hovered(ImGui::GetCursorScreenPos(), picture_size);
            ImVec4 tint = (hovered) ? GRAY : WHITE;
            ImGui::ImageWithBg(tex.id, picture_size, tex.uv0, tex.uv1, TRANSPARENT, tint);
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
                const ImVec4 bg_col = TRANSPARENT;
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
            bool pressed = imgui_aligned_text_button(song->title.c_str(), ImVec2(region_start.x + picture_size.x + padding, region_start.y), ImVec2(region_size.x - picture_size.x - padding, region_size.y), ALIGN_LEFT, ALIGN_CENTER);
            if (pressed)
                center_song = std::move(song);
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
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row];
            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (album == nullptr)
                return;
            if (imgui_aligned_text_button(album->name.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER))
                center_album = std::move(album);
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
            LruCacheRef<Song>& song = ctx.center.song_search_result.entries[row];
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

static void draw_artist_info()
{
    assert(ctx.center.type == SHOW_CENTER_ARTIST);

    LruCacheRef<Artist>& artist = ctx.center.artist;

    ImVec2 origin = ImGui::GetCursorPos();

    const ImVec2 album_art_size = ImVec2(210, 210);
    const float album_art_padding = 10.0f;

    static bool show_edit;
    static bool compact;

    ImGui::SetCursorPosX(origin.x + album_art_padding);
    ImGui::SetCursorPosY(origin.y + album_art_padding);

    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_default();
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, BLACK);

    ImGui::SameLine();

    ImVec2 region_start(
            origin.x + album_art_size.x + 2 * album_art_padding,
            origin.y + album_art_padding);
    ImGui::SetCursorPos(region_start);
    ImVec2 region_size(ImGui::GetContentRegionAvail().x, album_art_size.y);

    ImGui::BeginChild("artist_view", region_size);
    ImGui::SetWindowFontScale(2.0f); 
    const char* artist_name = (artist->name.size() > 0) ? artist->name.c_str() : "<No Artist>";
    imgui_aligned_text(artist_name, ImGui::GetCursorPos(), ImGui::GetContentRegionAvail());
    ImGui::SetWindowFontScale(1.0f); 

    const ImVec2 button_size = ImVec2(32, 32);
    ImGui::SetCursorPosY(origin.y + region_size.y - button_size.y - 2 * ImGui::GetStyle().FramePadding.y);
    if (imgui_image_button(ctx.textures.pin_button.id, button_size))
        mp_pin_artist(artist->id);

    ImGui::SameLine();
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size))
        show_edit = !show_edit;

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(0.0f, album_art_size.y + 2.0f * album_art_padding));

    if (show_edit)
    {
        ImGui::BeginChild("edited", ImGui::GetContentRegionAvail());

        static bool edited;

        if (ImGui::Button("Save"))
        {
            mp_update_artist(ctx.center.artist->id, ctx.center.edit.artist);
            edited = false;
        }
        ImGui::SameLine();
        ImGui::Text("Info%s", (edited) ? " [Unsaved Changes]" : "");

        ImGui::Text("Artist");
        ImGui::SameLine();
        edited |= ImGui::InputText("##Artist", ctx.center.edit.artist, sizeof(ctx.center.edit.artist));

        ImGui::EndChild();
    }
    else
    {
        static int current_search_type = static_cast<int>(SEARCH_TYPE_SONG);
        const char* search_types[] = {
            "Songs",
            "Albums"
        };

        const float padding = 10.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##search_combo", &current_search_type, search_types, std::size(search_types)))
        {
            if (current_search_type == SEARCH_TYPE_SONG)
                ctx.center.song_search_result = mp_search_songs(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
            else if (current_search_type == SEARCH_TYPE_ALBUM)
                ctx.center.album_search_result = mp_search_albums(ctx.search_query, SEARCH_RESULTS_PER_PAGE, 1);
        }

        ImGui::SameLine();
        if (current_search_type == SEARCH_TYPE_SONG)
        {
            if (ImGui::ArrowButton("Left", ImGuiDir_Left) && ctx.center.song_search_result.page_num > 1)
                ctx.center.song_search_result = mp_get_paginated_songs_from_artist(artist->id, SEARCH_RESULTS_PER_PAGE, --ctx.center.song_search_result.page_num);
            ImGui::SameLine();
            if (ImGui::ArrowButton("Right", ImGuiDir_Right) && ctx.center.song_search_result.page_num < ctx.center.song_search_result.num_pages)
                ctx.center.song_search_result = mp_get_paginated_songs_from_artist(artist->id, SEARCH_RESULTS_PER_PAGE, ++ctx.center.song_search_result.page_num);
            ImGui::SameLine();
            ImGui::Text("%d/%d (%d results)", ctx.center.song_search_result.page_num, ctx.center.song_search_result.num_pages, ctx.center.song_search_result.num_results);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
            draw_artist_info_songs();
        }
        else
        {
            if (imgui_image_button(ctx.textures.compact.id, ImVec2(12, 12)))
                compact = !compact;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
            ImGui::Separator();
            draw_album_table(ctx.center.albums, compact);
        }
    }
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
    switch (view_type)
    {
        case SHOW_CENTER_SONG:
        {
            set_center_view_song(mp_get_song(id));
            break;
        }
        case SHOW_CENTER_ALBUM:
        {
            set_center_view_album(mp_get_album(id));
            break;
        }
        case SHOW_CENTER_PLAYLIST:
        {
            set_center_view_playlist(mp_get_playlist(id));
            break;
        }
        case SHOW_CENTER_ARTIST:
        {
            set_center_view_artist(mp_get_artist(id));
            break;
        }
        default:
            break;
    }
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
    if (imgui_image_button(ctx.textures.home_button.id, button_size))
        set_center_view_home();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    if (imgui_image_button(ctx.textures.settings_button.id, button_size))
        set_center_view_settings();
    
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    if (imgui_image_button(ctx.textures.pencil_button.id, button_size))
        set_center_view_edit();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + advance);
    if (imgui_image_button(ctx.textures.search.id, button_size))
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

    static std::optional<std::future<std::string>> prompt{};
    if (ImGui::Button("Change DB") && !prompt.has_value()) 
    {
        prompt = std::async(std::launch::async,
            []() -> std::string {
                const std::string title {"Choose database"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"SQLite DB", "*.db"};
                const pfd::opt options = pfd::opt::none;
                std::vector<std::string> db_path = pfd::open_file(title, default_path, filters, options).result();
                return (db_path.size() > 0) ? db_path.front() : "";
            });
    }

    if (prompt.has_value() && prompt->wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready)
    {
        std::string db_path = prompt->get();
        if (db_path.size() > 0)
        {
            destroy_cache_references();
            mp_change_db(db_path);
        }
        else
        {
            SPDLOG_INFO("Change db prompt cancelled");
        }
        prompt.reset();
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
    static std::optional<std::future<void>> prompt{};
    if (ImGui::Button("Add Song") && !prompt.has_value()) 
    {
        prompt = std::async(std::launch::async,
            []() {
                const std::string title {"Choose files to read"};
                const std::string default_path = pfd::path::home();
                const std::vector<std::string> filters {"All Files", "*"};
                const pfd::opt options = pfd::opt::multiselect;
                std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters, options).result();
                mp_add_songs(std::move(song_paths));
            });
    }
    if (ImGui::Button("Add Folder") && !prompt.has_value()) 
    {
        prompt = std::async(std::launch::async,
            []() {
                const std::string title {"Select Any Directory"};
                const std::string default_path = pfd::path::home();
                const std::string folder_path = pfd::select_folder(title, default_path).result();
                mp_recursive_add_songs(folder_path);
            });
    }

    if (prompt.has_value() && prompt->wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready)
    {
        prompt.reset();
    }

    if (ImGui::Button("Create Song"))
        ;
    if (ImGui::Button("Create Album"))
    {
        set_center_view_album(mp_create_album());
        return;
    }
    if (ImGui::Button("Create Artist"))
    {
        set_center_view_artist(mp_create_artist());
        return;
    }
    if (ImGui::Button("Create Playlist"))
    {
        set_center_view_playlist(mp_create_playlist());
        return;
    }
    if (ImGui::Button("Reset"))
        mp_reset_loaded_songs();

    if (!mp_ctx.adding_songs)
    {
        ImGui::Text("Default text");
    }
    else if (prompt.has_value())
    {
        std::lock_guard lock{mp_ctx.adding_songs_mutex};
        ImGui::Text("%d / %d - %s", mp_ctx.num_songs_added, mp_ctx.num_songs_to_add, mp_ctx.adding_songs_current_path.c_str());
    }
    else
    {
        // TOOD improve this so it works with cache better
        static int page_num = 0;
        
        SearchResult<Song> page_result{};
        page_result.results_per_page = 20;
        page_result.num_results = mp_ctx.added_songs.size();
        page_result.num_pages = page_result.num_results / page_result.results_per_page;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);
        if (ImGui::ArrowButton("Left", ImGuiDir_Left))
            page_num--;
        ImGui::SameLine();
        if (ImGui::ArrowButton("Right", ImGuiDir_Right))
            page_num++;

        page_num = std::clamp(page_num, 0, page_result.num_pages);
        page_result.page_num = page_num;

        ImGui::SameLine();
        ImGui::Text("%d/%d (%d results)", page_result.page_num + 1, page_result.num_pages + 1, page_result.num_results);

        for (int i = page_result.page_num * page_result.results_per_page;
                i < (page_result.page_num + 1) * page_result.results_per_page && i < page_result.num_results; i++)
        {
            page_result.entries.emplace_back(mp_get_song(mp_ctx.added_songs[i]));
        }

        DrawSongTableParams params{};
        params.songs = &page_result.entries;

        draw_song_table(params);

        page_num = page_result.page_num;
    }
}

static void draw_lyrics()
{
    const float margin = 10.0f;
    ImGui::SetWindowFontScale(1.5f); 
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
    ImGui::SetWindowFontScale(1.0f); 
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
        case SHOW_CENTER_SEARCH_RESULT:
            draw_search_results();
            break;
        default:
            break;
    }
}

static void draw_right_side_queue()
{
    float padding = 8.0f;
    ImGui::SetCursorPosX(padding);
    ImGui::SetWindowFontScale(2.0f); 
    ImGui::Text("Queue");
    ImGui::SetWindowFontScale(1.0f); 
    int row = 0;
    ImVec2 root_pos = ImGui::GetCursorPos();
    root_pos.x = padding;
    float row_advance = 72.0f;
    auto list_song = [&row, &root_pos, &row_advance](const LruCacheRef<Song>& song, const ImVec4& color)
        {
            ImGui::SetCursorPos(ImVec2(root_pos.x, root_pos.y + row * row_advance));
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, color);
            ImGui::Image(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImVec2 cursor_pos = ImGui::GetCursorPos();
            if (imgui_aligned_text_button(song->title.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                set_center_view_song(mp_get_song(song->id));
            //ImGui::Text("%s", song->title.c_str());
            ImVec2 text_size = ImGui::CalcTextSize(song->title.c_str());
            cursor_pos.y += text_size.y;
            ImGui::SetCursorPos(cursor_pos);
            LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
            if (artist != nullptr)
            {
                if (imgui_aligned_text_button(artist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    set_center_view_artist(std::move(artist));
                cursor_pos.y += text_size.y;
                //cursor_pos.y += ImGui::CalcTextSize(artist->name.c_str()).y;
                //if (imgui_text_button(artist->name.c_str()))
                //    set_center_view_artist(std::move(artist));
            }
            ImGui::SetCursorPos(cursor_pos);
            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (album != nullptr)
            {
                if (imgui_aligned_text_button(album->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                    set_center_view_album(std::move(album));
                cursor_pos.y += text_size.y;
                //cursor_pos.y += ImGui::CalcTextSize(album->name.c_str()).y;
                //if (imgui_text_button(album->name.c_str()))
                //    set_center_view_album(std::move(album));
            }

            row++;
        };

    for (const LruCacheRef<Song> & song : mp_ctx.queue)
        list_song(song, RED);

    for (const auto& [song, track] : mp_ctx.group_queue)
        list_song(song, GREEN);
    
    for (const LruCacheRef<Song>& song : mp_ctx.autoplay_queue)
        list_song(song, BLUE);
}

static void draw_right_side()
{
    if (ctx.right.type == SHOW_RIGHT_QUEUE)
        draw_right_side_queue();
}

static void draw_player(const ImVec2 size)
{
    ImGui::SetCursorPos(ImVec2(14, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImVec2 album_art_size = ImVec2(100, 100);
    GLTexture tex = (mp_ctx.current_song != nullptr) 
        ? get_texture_from_song(mp_ctx.current_song->id)
        : get_texture_default();
    ImGui::ImageWithBg(tex.id, album_art_size, tex.uv0, tex.uv1, BLACK);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    const char* song_text = (mp_ctx.current_song == nullptr)
        ? "No Song Playing"
        : mp_ctx.current_song->title.c_str();

    constexpr float padding = 3.0f;

    ImVec2 song_info_offset = ImVec2(132.0f, 25.0f);
    float cursor_width = 450.0f;
    float song_cursor_x = (size.x - cursor_width) / 2.0f;

    ImGui::SetCursorPos(song_info_offset);
    {
        const ImVec2 region_start = song_info_offset;
        const ImVec2 region_size = ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y);
        if (mp_ctx.current_song != nullptr)
        {
            if (imgui_aligned_text_button(song_text, region_start, region_size, ALIGN_LEFT, ALIGN_LEFT))
                set_center_view_song(mp_get_song(mp_ctx.current_song->id));
        }
        else
            imgui_aligned_text(song_text, region_start, region_size, ALIGN_LEFT, ALIGN_LEFT);
    }

    song_info_offset.y += ImGui::CalcTextSize(song_text).y + padding;

    if (mp_ctx.current_song_artist != nullptr)
    {
        const char* artist_name = mp_ctx.current_song_artist->name.c_str();
        if (imgui_aligned_text_button(artist_name, song_info_offset, ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y), ALIGN_LEFT, ALIGN_LEFT))
            set_center_view_artist(mp_get_artist(mp_ctx.current_song_artist->id));
        song_info_offset.y += ImGui::CalcTextSize(artist_name).y + padding;
    }
    if (mp_ctx.current_song_album != nullptr)
    {
        const char* album_name = mp_ctx.current_song_album->name.c_str();
        ImGui::SetCursorPos(song_info_offset);
        if (imgui_aligned_text_button(album_name, song_info_offset, ImVec2(song_cursor_x - song_info_offset.x, ImGui::GetContentRegionAvail().y), ALIGN_LEFT, ALIGN_LEFT))
            set_center_view_album(mp_get_album(mp_ctx.current_song_album->id));
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
        GLTexture tex = (mp_ctx.mute) ? ctx.textures.mute : ctx.textures.volume;
        float right_edge_spacing = 10.0f;
        float icon_spacing = 4.0f;
        ImVec2 volume_icon_size = ImVec2(20.0f, 20.0f);
        float slider_y_offset = 30.0f;
        ImVec2 slider_size = ImVec2(175.0f, 5.0f);
        ImGui::SetCursorPos(ImVec2(size.x - slider_size.x - volume_icon_size.x - right_edge_spacing - icon_spacing, slider_y_offset));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (imgui_image_button(tex.id, volume_icon_size))
            mp_toggle_mute();
        ImGui::PopStyleVar();

        ImGui::SetCursorPos(ImVec2(size.x - slider_size.x - right_edge_spacing, slider_y_offset + (volume_icon_size.x - slider_size.y) / 2.0f));
        if (imgui_custom_slider_float("##volume", &mp_ctx.volume, 0.0f, 1.0f, slider_size))
            mp_update_volume();

        num_buttons = 4;
        width = 32.0f * num_buttons + spacing * (num_buttons - 1);
        constexpr float magic_spacing_constant = 8.0f;
        cursor_x = size.x - width - right_edge_spacing - magic_spacing_constant;

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
        if (imgui_image_button(ctx.textures.lyrics_button.id, button_size))
            set_center_view_lyrics(mp_ctx.current_song);
        cursor_x += advance;
        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        if (imgui_image_button(ctx.textures.clear_queue_button.id, button_size))
            mp_queue_clear();
        cursor_x += advance;

        ImVec4 tint = (ctx.right.type == SHOW_RIGHT_QUEUE) 
            ? SELECTED_COLOR_1
            : UNSELECTED_COLOR;
        ImGui::SetCursorPos(ImVec2(cursor_x, cursor_y));
        if (imgui_image_button(ctx.textures.show_queue_button.id, button_size, tint))
            toggle_right_view_queue();
    }
}

static void draw_debug_info()
{
    ImGui::PushStyleColor(ImGuiCol_Border, GRAY);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &ctx.show_debug_window))
    {
        ImGui::Text("%5.3f", ctx.debug.fps / 1000.0f);
        ImGui::Text("Song History size: %ld", mp_ctx.song_history.size());
        ImGui::Text("Max num slots: %d", ctx.textures.num_slots);
        ImGui::Text("Num slots in use: %zd", ctx.textures.num_slots - ctx.textures.free_slot_textures.size());

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

static void draw_left_side()
{
    int row = 0;
    float row_advance = 72.0f;

    float padding = 10.0f;
    ImGui::SetCursorPosX(padding);

    ImGui::SetWindowFontScale(2.0f); 
    ImGui::Text("Pins");
    ImGui::SetWindowFontScale(1.0f); 

    ImVec2 root_pos = ImGui::GetCursorPos();

    auto list_pin = [&row, &root_pos, &row_advance, padding](const Pin& pin)
        {
            GLTexture tex = get_texture_default();
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
                tex = get_texture_default();
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
            ImGui::Image(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImVec2 cursor_pos = ImGui::GetCursorPos();

            switch (pin.type)
            {
                case MPUID_SONG:
                {
                    if (imgui_aligned_text_button(pin.song->title.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                        set_center_view_song(mp_get_song(pin.song->id));
                    break;
                }
                case MPUID_PLAYLIST:
                {
                    if (imgui_aligned_text_button(pin.playlist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                        set_center_view_playlist(mp_get_playlist(pin.playlist->id));
                    break;
                }
                case MPUID_ALBUM:
                {
                    if (imgui_aligned_text_button(pin.album->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                        set_center_view_album(mp_get_album(pin.album->id));
                    break;
                }
                case MPUID_ARTIST:
                {
                    if (imgui_aligned_text_button(pin.artist->name.c_str(), cursor_pos, ImGui::GetContentRegionAvail()))
                        set_center_view_artist(mp_get_artist(pin.artist->id));
                    break;
                }
                default: break;
            }
            row++;
        };

    for (Pin& pin : mp_ctx.pins)
        list_pin(pin);
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

    const bool show_right_side = ctx.right.type != SHOW_RIGHT_NONE;
    ImGui::SetCursorPos(ImVec2(0.0f, header_height));
    ImGui::Separator();

    ImGui::SetNextWindowPos(ImVec2(0.0f, header_height));
    ImGui::SetNextWindowSize(ImVec2(display_size.x, interface_height));
    ImGui::BeginChild("##Interface", ImVec2(0.0f, interface_height));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("view", 1 + ctx.quick_access_open + show_right_side, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
    {
        if (ctx.quick_access_open)
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_NoSort);
        if (show_right_side)
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, 300);

        ImGui::TableNextRow();

        if (ctx.quick_access_open)
        {
            ImGui::TableNextColumn();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
            ImGui::BeginChild("##Left Side", ImGui::GetContentRegionAvail());
            draw_left_side();
            ImGui::EndChild();
        }

        ImGui::TableNextColumn();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
        ImGui::BeginChild("##Center", ImGui::GetContentRegionAvail());
        draw_center();
        ImGui::EndChild();

        if (show_right_side)
        {
            ImGui::TableNextColumn();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), header_height));
            ImGui::BeginChild("##Right Side", ImGui::GetContentRegionAvail());
            draw_right_side();
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

    if (ctx.show_debug_window)
        draw_debug_info();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ui_loop()
{
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

        update_textures();
        draw_imgui();
        double end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        ctx.debug.fps = end - start;

        glfwSwapBuffers(ctx.window);
    }
}

