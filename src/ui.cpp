// *****************************
// Includes
// *****************************

#include "ui.h"
#include "mp.h"
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
// Constants, Global Variables, Types
// *****************************

#define IMGUI_BLANK ImVec4(0.0f, 0.0f, 0.0f, 0.0f)

const char* vertex_shader = R"(
    #version 430 core
    layout (location = 0) in vec2 aTexCoords;

    out vec2 TexCoords;

    void main()
    {
        gl_Position = vec4(2.0 * (aTexCoords - 0.5), 0.0, 1.0); 
        TexCoords = aTexCoords;
    }  
)";

const char* fragment_shader = R"(
    #version 430 core
    out vec4 FragColor;
      
    in vec2 TexCoords;

    uniform sampler2D screenTexture;

    void main()
    { 
        FragColor = texture(screenTexture, TexCoords);
    }
)";

enum ViewEnum {
    SHOW_RIGHT_NONE,
    SHOW_RIGHT_QUEUE,
    SHOW_RIGHT_SONG_EDIT,
    SHOW_CENTER_NONE,
    SHOW_CENTER_SONG,
    SHOW_CENTER_ALBUM,
    SHOW_CENTER_PLAYLIST,
    SHOW_CENTER_ARTIST,
    SHOW_CENTER_PLAYLISTS,
    SHOW_CENTER_SEARCH_RESULT
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
        GLuint cover_texture;
        GLuint shader_program;
        GLuint vao;
        GLuint vbo;

        // GL texture objects that contain the slots
        std::vector<GLuint> textures;
        // Use PQ to acquire a slot idx that is drawn to in create_texture
        std::priority_queue<SlotID, std::vector<SlotID>, std::greater<SlotID>> free_slot_textures;
        // Use to keep track of the number of slots free_slot_textures has handed out
        int num_slots;
        // Use to map a song to a slot
        std::unordered_map<SongID, Slot> slot_map;
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
        GLTexture volume;
    } textures;

    struct Debug {
        double fps;
    } debug;

    std::mutex load_queue_lock;
    std::vector<SongID> load_queue;
    std::mutex unload_queue_lock;
    std::vector<SongID> unload_queue;

    bool show_demo_window;
    bool show_debug_window;

    struct {
        ViewEnum type;
        LruCacheRef<Song> song;
        LruCacheRef<Album> album;
        LruCacheRef<Playlist> playlist;
        LruCacheRef<Artist> artist;

        // For art of the shown type, so album art, playlist art, etc
        std::optional<Slot> art_slot;
        std::vector<Slot> album_art_slots;

        // if type is album or playlist, then write to song_tracks
        // otherwise, write to songs
        std::vector<SongTrack> song_tracks;
        std::vector<LruCacheRef<Song>> songs;
        std::vector<LruCacheRef<Album>> albums;
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
};

static UIContext ctx;

// *****************************
// ImGUI Helper Functions
// *****************************

enum AlignmentEnum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
};

enum MenuPopupEnum {
    MENU_END_POPUP,
    MENU_CLOSE_POPUP,
    MENU_CLOSE_TABLE
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Text("%s", text);
    ImGui::PopStyleColor();
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (hovered) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
    ImGui::SetCursorPos(cursor_pos);
    ImGui::Text("%s", text);
    if (hovered) ImGui::PopStyleColor();

    ImGui::PopStyleVar();
    return hovered && clicked;
}

static void imgui_aligned_text(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align, AlignmentEnum v_align)
{
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImGui::SetCursorPos(ImVec2(
                region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));
    ImGui::Text("%s", text);
    // Resetting the cursor is unnecessary
    //ImGui::SetCursorPos(ImVec2(region_start.x + region_size.x, region_start.y));
}

static bool imgui_aligned_text_button(const char* text, ImVec2 region_start, ImVec2 region_size, AlignmentEnum h_align, AlignmentEnum v_align)
{
    ImVec2 text_size = ImGui::CalcTextSize(text);
    ImGui::SetCursorPos(ImVec2(
                region_start.x + (region_size.x - text_size.x) / 2.0f * static_cast<int>(h_align),
                region_start.y + (region_size.y - text_size.y) / 2.0f * static_cast<int>(v_align)));
    return imgui_text_button(text);
}

static void imgui_custom_table(CustomTableParams& params)
{
    constexpr float separator_width = 8.0f;
    constexpr float separator_line_width = 2.0f;
    constexpr float separator_offset = -12.0f;
    ImVec2 origin = ImGui::GetCursorPos();
    ImVec2 origin_screen = ImGui::GetCursorScreenPos();
    params.col_offsets.push_back(&params.table_width);
    for (size_t i = 0; i < params.col_offsets.size() - 2; i++)
    {
        ImGui::PushID(i);
        ImVec2 region_start = ImVec2(origin.x + *params.col_offsets[i], origin.y);
        ImVec2 region_size = ImVec2(params.col_widths[i], params.header_row_height);
        params.header_row_col_callback[i](region_start, region_size);
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
            //*params.col_offsets[i+1] = std::clamp(
            //        ImGui::GetMousePos().x - origin_screen.x - separator_offset + separator_line_width / 2.0f,
            //        *params.col_offsets[i] + params.col_widths[i] - separator_offset, 
            //        *params.col_offsets[i+2] - params.col_widths[i+1] + separator_offset);
        }
        ImGui::GetWindowDrawList()->AddLineV(
                origin_screen.x + *params.col_offsets[i+1] + separator_offset - separator_line_width / 2.0f, 
                origin_screen.y + 5.0f, 
                origin_screen.y + params.header_row_height - 5.0f, 
                IM_COL32(56, 56, 56, 255),
                separator_line_width);
        ImGui::PopID();
    }
    params.header_row_col_callback.back()(
            ImVec2(origin.x + *params.col_offsets[params.num_cols-1], origin.y),
            ImVec2(params.col_widths[params.num_cols-1], params.header_row_height)
        );
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

        bool in_bounds = mouse_pos.x >= p_min.x && mouse_pos.x <= p_max.x && mouse_pos.y >= p_min.y && mouse_pos.y <= p_max.y;

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
            ImVec2 region_size(params.col_widths[col], params.row_height);
            ImGui::SetCursorPos(region_start);
            params.body_row_col_callback[col](region_start, region_size, row);
            ImGui::PopID();
        }
        ImGui::PopID();
        cursor_pos_y += params.row_height;
    }
    ImGui::EndChild();
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

static GLuint compile_shader_source(GLenum type, const char* data)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &data, NULL);
    glCompileShader(shader);
    GLint success{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) 
    {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        std::cout << info_log << '\n';
        exit(1);
    }

    return shader;
}

static GLuint compile_shader_program()
{
    GLuint shader_program = glCreateProgram();
    GLuint vert_shader = compile_shader_source(GL_VERTEX_SHADER, vertex_shader);
    GLuint frag_shader = compile_shader_source(GL_FRAGMENT_SHADER, fragment_shader);
    glAttachShader(shader_program, vert_shader);
    glAttachShader(shader_program, frag_shader);

    GLint success;
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    assert(success);

    glDetachShader(shader_program, vert_shader);
    glDetachShader(shader_program, frag_shader);
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    return shader_program;
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
    if (ctx.textures.slot_map.find(song_id) != ctx.textures.slot_map.end())
        return get_texture_from_slot(ctx.textures.slot_map[song_id]);
    return get_texture_default();
}

[[maybe_unused]] static GLTexture get_texture_from_album(AlbumID album_id)
{
    (void)album_id;
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

static void initialize_texture_fbo()
{
    ctx.textures.shader_program = compile_shader_program();

    glUseProgram(ctx.textures.shader_program);
    glGenVertexArrays(1, &ctx.textures.vao);
    glBindVertexArray(ctx.textures.vao);

    glGenBuffers(1, &ctx.textures.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.textures.vbo);
    const float vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glGenTextures(1, &ctx.textures.cover_texture);
    glBindTexture(GL_TEXTURE_2D, ctx.textures.cover_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    if (ctx.center.art_slot)
        delete_texture(ctx.center.art_slot.value());
    ctx.center.art_slot.reset();
    for (Slot& slot : ctx.center.album_art_slots)
        delete_texture(slot);
    ctx.center.album_art_slots.clear();
}

static void set_center_view_song(LruCacheRef<Song>&& song)
{
    ctx.center.type = SHOW_CENTER_SONG;
    ctx.center.artist = mp_get_artist_from_song(song->id);
    ctx.center.playlist = nullptr;
    ctx.center.album = mp_get_album_from_song(song->id);
    ctx.center.song = std::move(song);
    reset_center_view_containers();

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

    Art album_art = mp_get_album_art(ctx.center.album->id);
    if (album_art.data == nullptr)
        ctx.center.art_slot = ctx.textures.default_album_art_slot;
    else
        ctx.center.art_slot = create_texture(album_art.data);

    LruCacheRef<std::vector<SongTrackID>> tracks = mp_get_songs_from_album(ctx.center.album->id);
    for (auto & [song_id, track] : *tracks)
        ctx.center.song_tracks.emplace_back(mp_get_song(song_id), track);

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

    LruCacheRef<std::vector<SongTrackID>> tracks = mp_get_songs_from_playlist(ctx.center.playlist->id);
    for (auto & [song_id, track] : *tracks)
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

    LruCacheRef<std::vector<int>> albums = mp_get_albums_from_artist(ctx.center.artist->id);
    for (AlbumID album_id : *albums) {
        ctx.center.albums.emplace_back(mp_get_album(album_id));
        Art album_art = mp_get_album_art(album_id);
        Slot slot = (album_art.data == nullptr)
            ? ctx.textures.default_album_art_slot
            : create_texture(album_art.data);
        ctx.center.album_art_slots.push_back(slot);
    }

    push_history_entry(SHOW_CENTER_ARTIST, ctx.center.artist->id);
}

static void set_center_view_playlists()
{
    ctx.center.type = SHOW_CENTER_PLAYLISTS;
    ctx.center.song = nullptr;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    reset_center_view_containers();

    push_history_entry(SHOW_CENTER_PLAYLISTS, -1);
}

static void set_center_view_search_result()
{
    ctx.center.type = SHOW_CENTER_SEARCH_RESULT;
    ctx.center.song = nullptr;
    ctx.center.artist = nullptr;
    ctx.center.playlist = nullptr;
    ctx.center.album = nullptr;
    reset_center_view_containers();
}

static void toggle_right_view_queue()
{
    ctx.right.type = (ctx.right.type == SHOW_RIGHT_QUEUE) ? SHOW_RIGHT_NONE : SHOW_RIGHT_QUEUE;
    ctx.right.song = nullptr;
}

[[maybe_unused]] static void set_right_view_song_edit(LruCacheRef<Song>&& song)
{
    ctx.right.type = SHOW_RIGHT_SONG_EDIT;
    ctx.right.song = std::move(song);
    LruCacheRef<Album> album = mp_get_album(mp_get_album_from_song(ctx.right.song->id));
    LruCacheRef<Artist> artist = mp_get_artist(mp_get_artist_from_song(ctx.right.song->id));
    snprintf(ctx.right.song_title, sizeof(ctx.right.song_title), "%s", ctx.right.song->title.c_str());
    snprintf(ctx.right.song_artist, sizeof(ctx.right.song_artist), "%s", artist->name.c_str());
    snprintf(ctx.right.song_album, sizeof(ctx.right.song_album), "%s", album->name.c_str());
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
    initialize_default_texture_slot("assets/No-album-art.png");

    initialize_default_texture(&ctx.textures.play_button.id, "assets/play-edited.png");
    initialize_default_texture(&ctx.textures.queue_button.id, "assets/add-to-playlist.png");
    initialize_default_texture(&ctx.textures.skip_button.id, "assets/skip.png");
    initialize_default_texture(&ctx.textures.pause_button.id, "assets/pause.png");
    initialize_default_texture(&ctx.textures.repeat_button.id, "assets/repeat-edited.png");
    initialize_default_texture(&ctx.textures.shuffle_button.id, "assets/shuffle-edited.png");
    initialize_default_texture(&ctx.textures.autoplay_button.id, "assets/autoplay-edited.png");
    initialize_default_texture(&ctx.textures.volume.id, "assets/volume-up-edited.png");
    initialize_default_texture(&ctx.textures.show_queue_button.id, "assets/show-queue-edited.png");
    initialize_default_texture(&ctx.textures.clear_queue_button.id, "assets/clear-queue.png");
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
        for (int song_id : ctx.load_queue)
        {
            Art art = mp_get_song_art(song_id);
            if (art.data == nullptr)
                ctx.textures.slot_map[song_id] = ctx.textures.default_album_art_slot;
            else
                ctx.textures.slot_map[song_id] = create_texture(art.data);
            mp_free_art(&art);
        }
        double end = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        SPDLOG_INFO("Upaded textures in {} ms", (end - start) / 1000);

        ctx.load_queue.clear();
    }

    if (ctx.unload_queue.size() > 0)
    {
        std::lock_guard lock_guard{ctx.load_queue_lock};

        SongID song_id = ctx.unload_queue.front();
        auto it = ctx.textures.slot_map.find(song_id); 
        if (it == ctx.textures.slot_map.end())
        {
            std::swap(ctx.unload_queue.front(), ctx.unload_queue.back());
            return;
        }
        delete_texture(ctx.textures.slot_map[song_id]);
        ctx.textures.slot_map.erase(it);
        std::swap(ctx.unload_queue.front(), ctx.unload_queue.back());
        ctx.unload_queue.pop_back();
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

    initialize_texture_fbo();

    initialize_default_textures();
    
    assert(pfd::settings::available() && "Portable File Dialogs are not available on this platform.\n");
    //pfd::settings::verbose(true);
    IMGUI_CHECKVERSION();

    //const char* font_file_path = "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf";
    //const char* font_file_path = "/usr/share/fonts/truetype/noto/NotoSansAdlam-Regular.ttf";
    const char* font_file_path = "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    style.WindowBorderSize = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    const ImWchar icons_ranges[] = { 0xf000, 0xf3ff, 0 };
    io.Fonts->AddFontFromFileTTF(font_file_path, 20.0f, nullptr, icons_ranges);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);

    mp_ctx.song_constructor_callback = 
        [](Song* song) -> void
        {
            SPDLOG_INFO("Loading {} {}", song->id, song->title);
            std::lock_guard lock{ctx.load_queue_lock};
            ctx.load_queue.push_back(song->id);
        };

    mp_ctx.songs.set_destructor_callback(
        [](Song* song) -> void
        {
            SPDLOG_INFO("Unloading {} {}", song->id, song->title);
            std::lock_guard lock{ctx.unload_queue_lock};
            ctx.unload_queue.push_back(song->id);
        });

    ctx.playlists = mp_get_playlists();
}

void ui_cleanup()
{
    cleanup_textures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();

    // cannot destroy cacheref before cache
    ctx.center.song_tracks.clear();
    ctx.center.songs.clear();
    ctx.center.albums.clear();
    ctx.center.playlist.release();
    ctx.center.album.release();
    ctx.center.artist.release();
    ctx.playlists.clear();

    SPDLOG_INFO("UI cleaned up");
}

static void draw_search_results()
{
    ImGuiTableFlags flags = ImGuiTableFlags_None;
    flags |= ImGuiTableFlags_Resizable;
    flags |= ImGuiTableFlags_Hideable;
    flags |= ImGuiTableFlags_Sortable;
    flags |= ImGuiTableFlags_SortMulti;
    flags |= ImGuiTableFlags_RowBg;
    flags |= ImGuiTableFlags_BordersOuter;
    flags |= ImGuiTableFlags_BordersV;
    flags |= ImGuiTableFlags_NoBordersInBody;
    flags |= ImGuiTableFlags_ScrollY;

    //for (LruCacheRef<Song>& song : mp_ctx.search_result) {
    //    if (is_song_texture_loading(song->id)) {
    //        ImGui::Text("Loading...");
    //        return;
    //    }
    //}

    if (ImGui::BeginTable("All Songs", 5, flags, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("##Play Button", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 32);
        ImGui::TableSetupColumn("##Queue Button", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 32);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (LruCacheRef<Song>& song : mp_ctx.search_result)
        {
            ImGui::PushID(song->id);

            ImGui::TableNextColumn();
            if (ImGui::ImageButton("Play", ctx.textures.play_button.id, ImVec2(32, 32)))
                mp_play_song(song->id);;
            ImGui::TableNextColumn();
            if (ImGui::ImageButton("Queue", ctx.textures.queue_button.id, ImVec2(32, 32)))
                mp_queue_song(song->id);

            ImGui::TableNextColumn();
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::ImageWithBg(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());

            LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
            if (artist != nullptr && imgui_text_button(artist->name.c_str()))
                set_center_view_artist(std::move(artist));

            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (album != nullptr && imgui_text_button(album->name.c_str()))
                set_center_view_album(std::move(album));

            ImGui::TableNextColumn();
            if (ImGui::Button("Add To Playlist"))
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
                    mp_add_song_to_playlist(song->id, playlist->id);
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    set_center_view_playlist(std::move(playlist));
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_song_info()
{
    assert(ctx.center.song != nullptr);

    LruCacheRef<Song>& song = ctx.center.song;
    LruCacheRef<Artist>& artist = ctx.center.artist;
    LruCacheRef<Album>& album = ctx.center.album;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_song(song->id);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::SameLine();
    {
        ImGui::BeginChild("song_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(2.0f); 
        ImGui::Text("%s", song->title.c_str());
        ImGui::SetWindowFontScale(1.5f); 
        if (artist) {
            if (imgui_text_button(artist->name.c_str())) {
                set_center_view_artist(std::move(artist));
                ImGui::EndChild();
                return;
            }
        } else {
            ImGui::Text("<No Artist>");
        }
        if (album) {
            if (imgui_text_button(album->name.c_str())) {
                set_center_view_album(std::move(album));
                ImGui::EndChild();
                return;
            }
        } else {
            ImGui::Text("<No Album>");
        }
        ImGui::SetWindowFontScale(1.0f); 
        GLTexture tex = ctx.textures.play_button;
        if (ImGui::ImageButton("song_play", tex.id, ImVec2(32, 32)))
            mp_play_song(song->id);
        ImGui::SameLine();
        tex = ctx.textures.queue_button;
        if (ImGui::ImageButton("song_queue", tex.id, ImVec2(32, 32)))
            mp_queue_song(song->id);
        if (ImGui::Button("Add To Playlist"))
            ImGui::OpenPopup("add_to_playlist_popup");
        char length_str[256];
        int length = static_cast<int>(song->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);
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
                mp_add_song_to_playlist(song->id, playlist->id);
                set_center_view_playlist(std::move(playlist));
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }
}

static void draw_album_info()
{
    assert(ctx.center.album != nullptr);

    std::vector<SongTrack>& tracks = ctx.center.song_tracks;
    if (tracks.empty())
    {
        ImGui::Text("No songs in album");
        return;
    }

    LruCacheRef<Album>& album = ctx.center.album;
    LruCacheRef<Artist>& artist = ctx.center.artist;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_slot(ctx.center.art_slot.value());
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::SameLine();
    {
        ImGui::BeginChild("album_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(2.0f); 
        ImGui::Text("%s", album->name.c_str());
        ImGui::SetWindowFontScale(1.5f); 
        if (artist) {
            if (imgui_text_button(artist->name.c_str())) {
                set_center_view_artist(std::move(artist));
                ImGui::EndChild();
                return;
            }
        } else {
            ImGui::Text("<No Artist>");
        }
        ImGui::SetWindowFontScale(1.0f); 
        GLTexture tex = ctx.textures.play_button;
        if (ImGui::ImageButton("Album Play", tex.id, ImVec2(32, 32)))
            mp_play_album(album->id);
        ImGui::SameLine();
        tex = ctx.textures.queue_button;
        if (ImGui::ImageButton("Album Queue", tex.id, ImVec2(32, 32)))
            for (const auto& [song, track] : tracks)
                mp_queue_song(song->id);
        if (ImGui::Button("Add To Playlist"))
            ImGui::OpenPopup("add_to_playlist_popup");
        char length_str[256];
        int length = static_cast<int>(album->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);
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
        ImGui::EndChild();
    }

    CustomTableParams params{};
    params.num_cols = 3;
    params.num_rows = tracks.size();
    params.header_row_height = 40.0f;
    params.row_height = 40.0f;
    params.table_width = ImGui::GetContentRegionAvail().x;

    static float track_col_offset = 10.0f;
    static float title_col_offset = 70.0f;
    static float length_col_right_offset = 100.0f;
    float length_col_offset = params.table_width - length_col_right_offset;

    static float track_col_width = 50.0f;
    static float title_col_width = 400.0f;
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
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                const ImVec2 uv0 = ImVec2(0.0f, 0.0f);
                const ImVec2 uv1 = ImVec2(1.0f, 1.0f);
                const ImVec4 bg_col = IMGUI_BLANK;
                const ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                // IDK why ImGui::Image() doesnt work here maybe investigate some other time
                //ImGui::Image(tex.id, play_button_size, uv0, uv1, bg_col, tint_col);
                ImGui::ImageButton("Play", tex.id, play_button_size, uv0, uv1, bg_col, tint_col);
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
        [](ImVec2 region_start, ImVec2 region_size, int row)
        {
            imgui_aligned_text(tracks[row].song->title.c_str(), region_start, region_size, ALIGN_LEFT, ALIGN_CENTER);
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
}

static void draw_playlist_info()
{
    assert(ctx.center.playlist != nullptr);

    LruCacheRef<Playlist>& playlist = ctx.center.playlist;
    std::vector<SongTrack>& tracks = ctx.center.song_tracks;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_from_slot(ctx.textures.default_album_art_slot);
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::SameLine();
    {
        ImGui::BeginChild("playlist_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(2.0f); 
        ImGui::Text("%s", playlist->name.c_str());
        ImGui::SetWindowFontScale(1.0f); 
        static char playlist_name[256];
        if (ImGui::Button("Change Name")) {
            std::strncpy(playlist_name, playlist->name.c_str(), sizeof(playlist_name));
            ImGui::OpenPopup("change_playlist_name");
        }

        if (ImGui::Button("Queue"))
            for (const auto& [song, track] : tracks)
                mp_queue_song(song->id);

        if (ImGui::Button("Play"))
            mp_play_playlist(playlist->id);

        char length_str[256];
        int length = static_cast<int>(playlist->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);

        if (ImGui::BeginPopup("change_playlist_name")) {
            ImGui::Text("Edit name:");
            ImGui::InputText("##edit", playlist_name, IM_COUNTOF(playlist_name));
            if (ImGui::Button("Save") || ImGui::IsKeyPressed(ImGuiKey_Enter))
            {
                mp_rename_playlist(playlist->id, playlist_name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    if (tracks.size() == 0) {
        ImGui::Text("No Songs");
        return;
    }

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
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                const ImVec2 uv0 = ImVec2(0.0f, 0.0f);
                const ImVec2 uv1 = ImVec2(1.0f, 1.0f);
                const ImVec4 bg_col = IMGUI_BLANK;
                const ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                // IDK why ImGui::Image() doesnt work here maybe investigate some other time
                //ImGui::Image(tex.id, play_button_size, uv0, uv1, bg_col, tint_col);
                ImGui::ImageButton("Play", tex.id, play_button_size, uv0, uv1, bg_col, tint_col);
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
        [](ImVec2 region_start, ImVec2 region_size, int row)
        {
            auto& [song, track] = tracks[row];
            ImVec2 picture_size(48, 48);
            GLTexture tex = get_texture_from_song(song->id);
            ImGui::SetCursorPos(ImVec2(region_start.x, region_start.y));
            ImGui::Image(tex.id, picture_size, tex.uv0, tex.uv1);
            imgui_aligned_text(tracks[row].song->title.c_str(), ImVec2(region_start.x + picture_size.x + 5.0f, region_start.y), region_size, ALIGN_LEFT, ALIGN_CENTER);
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

    if (center_artist != nullptr)
        set_center_view_artist(std::move(center_artist));
    if (center_album != nullptr)
        set_center_view_album(std::move(center_album));
}

static void draw_playlists()
{
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("All Playlists", 3, flags))
    {
        ImGui::TableSetupColumn("Art", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_NoSort);
        for (LruCacheRef<Playlist>& playlist : ctx.playlists)
        {
            ImGui::PushID(playlist->id);
            ImGui::TableNextColumn();
            GLTexture tex = get_texture_default();
            ImGui::ImageWithBg(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TableNextColumn();
            ImGui::Text("%s", playlist->name.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Button("Open"))
                set_center_view_playlist(mp_get_playlist(playlist->id));
            ImGui::PopID();
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Create Playlist"))
        {
            LruCacheRef<Playlist> playlist = mp_create_playlist();
            ctx.playlists.push_back(mp_get_playlist(playlist->id));
            set_center_view_playlist(std::move(playlist));
        }
        ImGui::EndTable();
    }
}

[[maybe_unused]] static void draw_artist_info_old()
{
    assert(ctx.center.artist != nullptr);

    LruCacheRef<Artist>& artist = ctx.center.artist;
    ImGui::SetWindowFontScale(4.0f); 
    ImGui::Text("%s", artist->name.c_str());
    ImGui::SetWindowFontScale(1.0f); 

    LruCacheRef<std::vector<int>> album_ids = mp_get_albums_from_artist(artist->id);
    LruCacheRef<std::vector<int>> song_ids = mp_get_songs_from_artist(artist->id);

    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    int width = ImGui::GetContentRegionAvail().x / 2;
    int height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("All Songs", 4, flags, ImVec2(width, height)))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (SongID song_id : *song_ids)
        {
            LruCacheRef<Song> song = mp_get_song(song_id);
            ImGui::TableNextColumn();
            ImGui::PushID(song_id);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();

            GLTexture tex = get_texture_from_song(song_id);
            ImGui::ImageWithBg(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());

            LruCacheRef<Artist> artist = mp_get_artist_from_song(song_id);
            if (artist != nullptr && ImGui::Button(artist->name.c_str()))
                set_center_view_artist(std::move(artist));
            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album"))
                set_center_view_album(mp_get_album_from_song(song_id));
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                //for (const Playlist& playlist : mp_ctx.playlists)
                //{
                //    ImGui::PushID(playlist.id);
                //    if (ImGui::Button(playlist.name.c_str()))
                //        mp_add_song_id_to_playlist_id(song_id, playlist.id);
                //    ImGui::PopID();
                //}
                if (ImGui::Button("Create Playlist"))
                {
                    LruCacheRef<Playlist> playlist = mp_create_playlist();
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    mp_add_song_to_playlist(song_id, playlist->id);
                    set_center_view_playlist(std::move(playlist));
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SameLine();
    if (ImGui::BeginTable("All Albums", 2, flags, ImVec2(width, height)))
    {
        ImGui::TableSetupColumn("Tmp", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        for (AlbumID album_id : *album_ids)
        {
            LruCacheRef<Album> album = mp_get_album(album_id);
            ImGui::PushID(album_id);
            ImGui::TableNextColumn();
            ImGui::Text("tmp");
            ImGui::TableNextColumn();
            ImGui::Text("%s", album->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) 
                set_center_view_album(mp_get_album(album->id));
            ImGui::SameLine();
            if (ImGui::Button("Queue")) {
                auto song_tracks = mp_get_songs_from_album(album_id);
                for (const auto& [song_id, track] : *song_tracks)
                    mp_queue_song(song_id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Play"))
                mp_play_album(album_id);
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                //for (const Playlist& playlist : mp_ctx.playlists)
                //{
                //    ImGui::PushID(playlist.id);
                //    if (ImGui::Button(playlist.name.c_str()))
                //        mp_add_album_id_to_playlist_id(album->id, playlist.id);
                //    ImGui::PopID();
                //}
                if (ImGui::Button("Create Playlist"))
                {
                    LruCacheRef<Playlist> playlist = mp_create_playlist();
                    mp_add_album_to_playlist(album->id, playlist->id);
                    ctx.playlists.push_back(mp_get_playlist(playlist->id));
                    set_center_view_playlist(std::move(playlist));
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_artist_info()
{
    assert(ctx.center.type == SHOW_CENTER_ARTIST);

    LruCacheRef<Artist>& artist = ctx.center.artist;
    std::vector<LruCacheRef<Album>>& albums = ctx.center.albums;
    std::vector<Slot>& album_art_slots = ctx.center.album_art_slots;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    const ImVec2 size = ImVec2(210, 210);
    GLTexture tex = get_texture_default();
    ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    ImGui::SameLine();
    ImGui::BeginChild("song_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
    ImGui::SetWindowFontScale(2.0f); 
    ImGui::Text("%s", artist->name.c_str());
    ImGui::EndChild();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
    ImGui::SetWindowFontScale(1.2f); 
    ImGui::Text("Albums");
    ImGui::SetWindowFontScale(1.0f); 
    float row_width = ImGui::GetContentRegionAvail().x;
    ImVec2 origin = ImGui::GetCursorPos();

    for (size_t i = 0; i < albums.size(); i++)
    {
        if (origin.x + 170 * (i+1) > row_width)
            break;
        LruCacheRef<Album>& album = albums[i];
        ImGui::PushID(i);
        ImGui::SetCursorPos(ImVec2(origin.x + 170 * i, origin.y));
        tex = get_texture_from_slot(album_art_slots[i]);
        ImGui::ImageWithBg(tex.id, ImVec2(160, 160), tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            set_center_view_album(std::move(album));
            ImGui::PopID();
            return;
        }
        ImGui::SetCursorPos(ImVec2(origin.x + 170 * i, origin.y + 160));
        ImGui::PushTextWrapPos(origin.x + 170 * i + 160);
        ImGui::TextWrapped("%s", album->name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.0f);
    ImGui::Text("Songs");
    SearchResult<Song> search_result = mp_get_paginated_songs_from_artist(artist->id, 10);
    for (LruCacheRef<Song>& song : search_result.entries)
    {
        ImGui::Text("%s", song->title.c_str());
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
        case SHOW_CENTER_PLAYLISTS:
        {
            break;
        }
        default:
            break;
    }
}

static void draw_center()
{
    const bool history_non_empty = ctx.view_history.empty();
    if (history_non_empty)
        ImGui::BeginDisabled();

    if (ImGui::ArrowButton("History Back", ImGuiDir_Left))
        pop_history();
    ImGui::SameLine();
    if (ImGui::ArrowButton("History Forward", ImGuiDir_Right))
        pop_history();

    if (history_non_empty)
        ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("Search");
    static char search_query[256];
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);

    static int page_num;
    constexpr int page_limit = 20;
    if (ImGui::InputTextWithHint("##", "Search...", search_query, sizeof(search_query))) 
    {
        page_num = 0;
        mp_search_songs(search_query, page_limit, page_num);
        set_center_view_search_result();
    }
    if (ImGui::IsItemClicked())
        snprintf(search_query, sizeof(search_query), "");

    if (ctx.center.type == SHOW_CENTER_SEARCH_RESULT)
    {
        int num_pages = mp_ctx.num_results / page_limit;
        ImGui::SameLine();
        if (ImGui::Button("Left") && page_num > 0)
            mp_search_songs(search_query, page_limit, --page_num);
        ImGui::SameLine();
        if (ImGui::Button("Right") && page_num < num_pages)
            mp_search_songs(search_query, page_limit, ++page_num);
        ImGui::SameLine();
        ImGui::Text("%d/%d", page_num + 1, num_pages + 1);
    }

    if (ctx.center.type == SHOW_CENTER_SONG)
        draw_song_info();
    else if (ctx.center.type == SHOW_CENTER_ALBUM)
        draw_album_info();
    else if (ctx.center.type == SHOW_CENTER_PLAYLIST)
        draw_playlist_info();
    else if (ctx.center.type == SHOW_CENTER_PLAYLISTS)
        draw_playlists();
    else if (ctx.center.type == SHOW_CENTER_ARTIST)
        draw_artist_info();
    else if (ctx.center.type == SHOW_CENTER_SEARCH_RESULT)
        draw_search_results();
}

[[maybe_unused]] static void draw_right_side_song_edit()
{
    ImGui::Text("Unused");
//    if (ImGui::Button("Close"))
//    {
//        ctx.right.type = SHOW_RIGHT_NONE;
//        return;
//    }
//    [[maybe_unused]] LruCacheRef<Song>& song = ctx.right.song;
//    if (ImGui::ImageButton("Press", ctx.textures.default_album_art.id, ImVec2(LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE)))
//    {
//        //const std::string title {"Choose files to read"};
//        //const std::string default_path = pfd::path::home();
//        //const std::vector<std::string> filters {"All Files", "*"};
//        //std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters).result();
//        //if (song_paths.size() > 0)
//        //    mp_song_front_cover_update(song->id, song_paths.front());
//    }
//
//    //ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInput, false);
//    ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
//
//    ImGui::Text("Title: ");
//    ImGui::SameLine();
//    ctx.right.changed |= ImGui::InputText("aa", ctx.right.song_title, STRING_LENGTH, flags);
//
//    ImGui::Text("Artist: ");
//    ImGui::SameLine();
//    ctx.right.changed |= ImGui::InputText("bb", ctx.right.song_artist, STRING_LENGTH, flags);
//
//    ImGui::Text("Album: ");
//    ImGui::SameLine();
//    ctx.right.changed |= ImGui::InputText("cc", ctx.right.song_album, STRING_LENGTH, flags);
//
//    if (ImGui::Button("Save"))
//    {
//        //mp_song_update(song->id, ctx.right.song_title, ctx.right.song_artist, ctx.right.song_album, nullptr);
//        ctx.right.changed = false;
//    }
//    if (ctx.right.changed)
//    {
//        ImGui::SameLine();
//        ImGui::Text("Unsaved Changes");
//    }
}

static void draw_right_side_queue()
{
    ImGui::SetWindowFontScale(2.0f); 
    ImGui::Text("Queue");
    ImGui::SetWindowFontScale(1.0f); 
    int row = 0;
    ImVec2 root_pos = ImGui::GetCursorPos();
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
            ImGui::Text("%s", song->title.c_str());
            ImVec2 text_size = ImGui::CalcTextSize(song->title.c_str());
            cursor_pos.y += text_size.y;
            ImGui::SetCursorPos(cursor_pos);
            LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
            if (artist != nullptr)
            {
                cursor_pos.y += ImGui::CalcTextSize(artist->name.c_str()).y;
                if (imgui_text_button(artist->name.c_str()))
                    set_center_view_artist(std::move(artist));
            }
            ImGui::SetCursorPos(cursor_pos);
            LruCacheRef<Album> album = mp_get_album_from_song(song->id);
            if (album != nullptr)
            {
                cursor_pos.y += ImGui::CalcTextSize(album->name.c_str()).y;
                if (imgui_text_button(album->name.c_str()))
                    set_center_view_album(std::move(album));
            }

            row++;
        };

    for (const LruCacheRef<Song> & song : mp_ctx.queue)
        list_song(song, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));

    for (const auto& [song, track] : mp_ctx.group_queue)
        list_song(song, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    
    for (const LruCacheRef<Song>& song : mp_ctx.autoplay_queue)
        list_song(song, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
}

static void draw_right_side()
{
    if (ctx.right.type == SHOW_RIGHT_QUEUE)
        draw_right_side_queue();
    else if (ctx.right.type == SHOW_RIGHT_SONG_EDIT)
        draw_right_side_song_edit();
}

static void draw_player(const ImVec2 size)
{
    (void)size;
    ImGui::SetCursorPos(ImVec2(14, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImVec2 album_art_size = ImVec2(100, 100);
    GLTexture tex = (mp_ctx.current_song != nullptr) 
        ? get_texture_from_song(mp_ctx.current_song->id)
        : get_texture_default();
    ImGui::ImageWithBg(tex.id, album_art_size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    const char* song_text = (mp_ctx.current_song == nullptr)
        ? "No Song Playing"
        : mp_ctx.current_song->title.c_str();

    float offset = 25.0f;
    constexpr float padding = 3.0f;

    ImGui::SetCursorPos(ImVec2(132, offset));
    ImGui::Text("%s", song_text);
    offset += ImGui::CalcTextSize(song_text).y + padding;

    if (mp_ctx.current_song_artist != nullptr)
    {
        const char* artist_name = mp_ctx.current_song_artist->name.c_str();
        ImGui::SetCursorPos(ImVec2(132, offset));
        if (imgui_text_button(artist_name))
            set_center_view_artist(mp_get_artist(mp_ctx.current_song_artist->id));
        offset += ImGui::CalcTextSize(artist_name).y + padding;
    }
    if (mp_ctx.current_song_album != nullptr)
    {
        const char* album_name = mp_ctx.current_song_album->name.c_str();
        ImGui::SetCursorPos(ImVec2(132, offset));
        if (imgui_text_button(album_name))
            set_center_view_album(mp_get_album(mp_ctx.current_song_album->id));
    }

    float cursor_width = 450.0f;
    char cursor_str[256];
    int cursor = static_cast<int>(mp_ctx.current_song_cursor);
    int length = static_cast<int>(mp_ctx.current_song_length);
    snprintf(cursor_str, sizeof(cursor_str), "%d:%02d / %d:%02d", cursor / 60, cursor % 60, length / 60, length % 60);
    ImGui::SetCursorPos(ImVec2((size.x - cursor_width) / 2.0f, 20.0f));
    ImGui::SetNextItemWidth(cursor_width);
    if (ImGui::SliderFloat("##Cursor", &mp_ctx.current_song_cursor, 0.0f, mp_ctx.current_song_length, cursor_str, ImGuiSliderFlags_None))
        mp_update_cursor();

    const ImVec2 button_size = ImVec2(32.0f, 32.0f);
    const float spacing = 16.0f;
    const float width = 32 * 6 + spacing * 5;
    const float advance = 32 + spacing;
    float cursor_x = (size.x - width) / 2.0f;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    bool key_pressed = !ImGui::GetIO().WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_F9));
    tex = (mp_ctx.paused) ? ctx.textures.play_button : ctx.textures.pause_button;
    if (ImGui::ImageButton("Pause/Resume Button", tex.id, ImVec2(32, 32)) || key_pressed)
        mp_pause_or_resume();
    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    if (ImGui::ImageButton("Previous Button", ctx.textures.skip_button.id, button_size, ImVec2(1.0f, 0.0f), ImVec2(0.0f, 1.0f)))
        mp_play_previous();
    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    if (ImGui::ImageButton("Next Button", ctx.textures.skip_button.id, button_size))
        mp_play_next();

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    {
        ImVec2 uv0 = ImVec2(0,0);
        ImVec2 uv1 = ImVec2(1,1);
        ImVec4 bg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4 tint = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (mp_ctx.loop_mode == LOOP_GROUP)
            tint.x = 1.0f;
        else if (mp_ctx.loop_mode == LOOP_TRACK)
            tint.y = 1.0f;
        if (ImGui::ImageButton("Repeat Button", ctx.textures.repeat_button.id, button_size, uv0, uv1, bg, tint))
            mp_ctx.loop_mode = static_cast<LoopMode>((static_cast<int>(mp_ctx.loop_mode) + 1) % 3);
    }

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    {
        ImVec2 uv0 = ImVec2(0,0);
        ImVec2 uv1 = ImVec2(1,1);
        ImVec4 bg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4 tint = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (mp_ctx.shuffle)
            tint.x = 1.0f;
        if (ImGui::ImageButton("Shuffle Button", ctx.textures.shuffle_button.id, button_size, uv0, uv1, bg, tint))
            mp_toggle_shuffle();
    }

    cursor_x += advance;
    ImGui::SetCursorPos(ImVec2(cursor_x, 50.0f));
    {
        ImVec2 uv0 = ImVec2(0,0);
        ImVec2 uv1 = ImVec2(1,1);
        ImVec4 bg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4 tint = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (mp_ctx.autoplay)
            tint.x = 1.0f;
        if (ImGui::ImageButton("Autoplay Button", ctx.textures.autoplay_button.id, button_size, uv0, uv1, bg, tint))
            mp_toggle_autoplay();
    }

    {
        float right_edge_spacing = 10.0f;
        float icon_spacing = 4.0f;
        float volume_slider_size = 150.0f;
        float volume_icon_size = 20.0f;
        ImGui::SetCursorPos(ImVec2(size.x - volume_slider_size - volume_icon_size - right_edge_spacing - icon_spacing, 20.0f));
        ImGui::Image(ctx.textures.volume.id, ImVec2(volume_icon_size, volume_icon_size));
        ImGui::SetCursorPos(ImVec2(size.x - volume_slider_size - right_edge_spacing, 20.0f));
        ImGui::SetNextItemWidth(volume_slider_size);
        if (ImGui::SliderFloat("##Volume", &mp_ctx.volume, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_None))
            mp_update_volume();

        ImVec2 uv0 = ImVec2(0,0);
        ImVec2 uv1 = ImVec2(1,1);
        ImVec4 bg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4 tint = (ctx.right.type == SHOW_RIGHT_QUEUE) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        constexpr float magic_spacing_constant = 8.0f;
        ImGui::SetCursorPos(ImVec2(size.x - button_size.x - right_edge_spacing - magic_spacing_constant, 50.0f));
        if (ImGui::ImageButton("Show Queue Button", ctx.textures.show_queue_button.id, button_size, uv0, uv1, bg, tint))
            toggle_right_view_queue();
        ImGui::SetCursorPos(ImVec2(size.x - 2 * button_size.x - right_edge_spacing - spacing - magic_spacing_constant, 50.0f));
        if (ImGui::ImageButton("Clear Queue Button", ctx.textures.clear_queue_button.id, button_size))
            mp_queue_clear();
    }
}

static void draw_debug_info()
{
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &ctx.show_debug_window))
    {
        ImGui::Text("%f", ctx.debug.fps);
        if (ctx.textures.textures.size() > 0)
            ImGui::Image(ctx.textures.textures[0], ImVec2(512, 512));
        ImGui::Text("Song History size: %ld", mp_ctx.song_history.size());
        ImGui::End();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void draw_left_side()
{
    if (ImGui::Button("Add Song")) 
    {
        const std::string title {"Choose files to read"};
        const std::string default_path = pfd::path::home();
        const std::vector<std::string> filters {"All Files", "*"};
        const pfd::opt options = pfd::opt::multiselect;
        std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters, options).result();
        mp_add_songs(song_paths);
    }
    if (ImGui::Button("Add Folder")) 
    {
        const std::string title {"Select Any Directory"};
        const std::string default_path = pfd::path::home();
        const std::string folder_path = pfd::select_folder(title, default_path).result();
        mp_recursive_add_songs(folder_path);
    }
    if (ImGui::Button("Playlists"))
        set_center_view_playlists();
    if (ImGui::Button("Create Song"))
        ;
    if (ImGui::Button("Create Album"))
        ;
    if (ImGui::Button("Create Artist"))
        ;
    if (ImGui::Button("Create Playlist"))
        ;
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
    ImGui::Begin("UMP", &window_open, window_flags);

    static bool player_open = true;
    float player_height = (player_open) ? 128.0f : 0.0f;
    float interface_height = display_size.y - player_height;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(display_size.x, interface_height));
    ImGui::BeginChild("##Interface", ImVec2(0.0f, interface_height));

    const bool show_right_side = ctx.right.type != SHOW_RIGHT_NONE;
    if (ImGui::BeginTable("view", 2 + show_right_side, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_NoSort);
        if (show_right_side)
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, 300);

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), 0.0f));
        ImGui::BeginChild("##Left Side", ImGui::GetContentRegionAvail());
        draw_left_side();
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), 0.0f));
        ImGui::BeginChild("##Center", ImGui::GetContentRegionAvail());
        draw_center();
        ImGui::EndChild();

        if (show_right_side)
        {
            ImGui::TableNextColumn();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetCursorPosX(), 0.0f));
            ImGui::BeginChild("##Right Side", ImGui::GetContentRegionAvail());
            draw_right_side();
            ImGui::EndChild();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();

    if (player_open)
    {
        ImGui::SetCursorPos(ImVec2(0.0f, interface_height));
        ImGui::Separator();
        ImGui::SetNextWindowPos(ImVec2(0.0f, interface_height));
        ImGui::SetNextWindowSize(ImVec2(display_size.x, player_height));
        const ImVec2 size = ImVec2(display_size.x, player_height);
        ImGui::BeginChild("##Child", size);
        draw_player(size);
        ImGui::EndChild();
    }

    ImGui::End();

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

        double start = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
        double end = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        ctx.debug.fps = end - start;

        glfwSwapBuffers(ctx.window);
    }
}

