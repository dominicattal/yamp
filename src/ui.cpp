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
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STRING_LENGTH 512
#define TEXTURE_SIZE_BITS 12
#define LARGE_COVER_ART_SIZE_BITS 8
#define SMALL_COVER_ART_SIZE_BITS 6
#define TEXTURE_SIZE (1<<TEXTURE_SIZE_BITS)
#define LARGE_COVER_ART_SIZE (1<<LARGE_COVER_ART_SIZE_BITS)
#define SMALL_COVER_ART_SIZE (1<<SMALL_COVER_ART_SIZE_BITS)
#define SLOTS_PER_TEXTURE (1<<(TEXTURE_SIZE_BITS<<1)>>(LARGE_COVER_ART_SIZE_BITS<<1))

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
        //FragColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
)";

enum ViewEnum {
    SHOW_RIGHT_NONE,
    SHOW_RIGHT_ALBUM,
    SHOW_RIGHT_SONG,
    SHOW_RIGHT_PLAYLIST,
    SHOW_CENTER_NONE,
    SHOW_CENTER_ALBUM,
    SHOW_CENTER_PLAYLIST,
    SHOW_CENTER_ARTIST,
    SHOW_CENTER_SEARCH_RESULT
};

struct GLTexture {
    GLuint id;
    int width;
    int height;
};

struct GLTexture2 {
    GLuint id;
    ImVec2 uv0;
    ImVec2 uv1;
};

struct UIContext {
    GLFWwindow* window;

    struct TextureInfo {
        // create TEXTURE_SIZE x TEXTURE_SIZE textures that have pages for LARGE_COVER_SIZE x LARGE_COVER_SIZE
        // images to upload. 
        GLuint fbo;
        GLuint fbo_texture;
        GLuint cover_texture;
        GLuint shader_program;
        GLuint vao;
        GLuint vbo;
        std::priority_queue<int, std::vector<int>, std::greater<int>> texture_slots;
        int texture_count;
        std::vector<GLuint> textures;
        std::unordered_map<int, int> song_map;
        GLTexture default_album_art;
        GLTexture play_button;
        GLTexture queue_button;
    } textures;

    std::mutex load_queue_lock;
    std::vector<std::pair<int, std::string>> load_queue;
    std::mutex unload_queue_lock;
    std::vector<int> unload_queue;

    bool show_demo_window;

    int right_side;
    GLuint right_side_texture;
    int right_side_song_id;
    char right_side_song_title[STRING_LENGTH];
    char right_side_song_artist[STRING_LENGTH];
    char right_side_song_album[STRING_LENGTH];
    bool right_side_changed;

    int center;
    LruCacheRef<Album> open_album;
    LruCacheRef<Playlist> open_playlist;
    LruCacheRef<Artist> open_artist;
};

static UIContext ctx;

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

static GLTexture2 get_texture_from_slot_idx(int slot_idx)
{
    const float size = static_cast<float>(LARGE_COVER_ART_SIZE) / TEXTURE_SIZE;
    const int slots_across = TEXTURE_SIZE / LARGE_COVER_ART_SIZE;
    const float x_off = static_cast<float>((slot_idx % SLOTS_PER_TEXTURE) % slots_across * LARGE_COVER_ART_SIZE) / TEXTURE_SIZE;
    const float y_off = static_cast<float>((slot_idx % SLOTS_PER_TEXTURE) / slots_across * LARGE_COVER_ART_SIZE) / TEXTURE_SIZE;
    return { 
        .id = ctx.textures.textures[slot_idx / SLOTS_PER_TEXTURE],
        .uv0 = ImVec2(x_off, y_off),
        .uv1 = ImVec2(x_off + size, y_off + size),
    };
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

    glGenFramebuffers(1, &ctx.textures.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.textures.fbo);

    glGenTextures(1, &ctx.textures.fbo_texture);
    glBindTexture(GL_TEXTURE_2D, ctx.textures.fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx.textures.fbo_texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenTextures(1, &ctx.textures.cover_texture);
    glBindTexture(GL_TEXTURE_2D, ctx.textures.cover_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static int create_texture(unsigned char* data, int width, int height)
{
    (void)data; (void)width; (void)height;
    if (ctx.textures.texture_slots.size() == 0)
        ctx.textures.texture_slots.push(ctx.textures.texture_count++);

    GLuint id;
    const int slot_idx = ctx.textures.texture_slots.top();
    ctx.textures.texture_slots.pop();
    const size_t texture_idx = slot_idx / SLOTS_PER_TEXTURE;
    assert(texture_idx <= ctx.textures.textures.size());
    if (texture_idx == ctx.textures.textures.size())
    {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_SIZE, TEXTURE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        ctx.textures.textures.push_back(id);
    }
    id = ctx.textures.textures[texture_idx];
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.textures.fbo);
    glViewport(0, 0, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(ctx.textures.shader_program);
    glBindTexture(GL_TEXTURE_2D, ctx.textures.cover_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindVertexArray(ctx.textures.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int window_width, window_height;
    glfwGetWindowSize(ctx.window, &window_width, &window_height);
    glViewport(0, 0, window_width, window_height);

    const int slots_across = TEXTURE_SIZE / LARGE_COVER_ART_SIZE;
    const int x_off = (slot_idx % SLOTS_PER_TEXTURE) % slots_across * LARGE_COVER_ART_SIZE;
    const int y_off = (slot_idx % SLOTS_PER_TEXTURE) / slots_across * LARGE_COVER_ART_SIZE;
    glCopyImageSubData(
        ctx.textures.fbo_texture, GL_TEXTURE_2D, 0, 0, 0, 0,
        id,                       GL_TEXTURE_2D, 0, x_off, y_off, 0.0f,
        LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 1);

    return slot_idx;
}

static void delete_texture(int slot_idx)
{
    ctx.textures.texture_slots.push(slot_idx);
}

static void initialize_default_texture(GLuint* id, const char* path, int* width, int* height)
{
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int nc;
    FILE* fptr = fopen(path, "r");
    unsigned char* data = stbi_load_from_file(fptr, width, height, &nc, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    fclose(fptr);
}

static void initialize_default_textures()
{
    initialize_default_texture(&ctx.textures.default_album_art.id, "assets/No-album-art.png", &ctx.textures.default_album_art.width, &ctx.textures.default_album_art.height);
    initialize_default_texture(&ctx.textures.play_button.id, "assets/play.png", &ctx.textures.play_button.width, &ctx.textures.play_button.height);
    initialize_default_texture(&ctx.textures.queue_button.id, "assets/add-to-playlist.png", &ctx.textures.queue_button.width, &ctx.textures.queue_button.height);
    glGenTextures(1, &ctx.right_side_texture);
}

static void cleanup_textures()
{
    glDeleteTextures(1, &ctx.right_side_texture);
    glDeleteTextures(1, &ctx.textures.default_album_art.id);
}

[[maybe_unused]] static void song_constructor_callback(Song* song)
{
    (void)song;
}

static void update_textures()
{
    if (ctx.load_queue.size() > 0)
    {
        std::lock_guard lock_guard{ctx.load_queue_lock};
        
        for (auto& [song_id, path] : ctx.load_queue)
        {
            FrontCover front_cover = mp_song_front_cover_load(path);
            int slot_idx = create_texture(front_cover.data, front_cover.width, front_cover.height);
            ctx.textures.song_map[song_id] = slot_idx;
            mp_song_front_cover_free(&front_cover);
        }
        ctx.load_queue.clear();
    }

    if (ctx.unload_queue.size() > 0)
    {
        std::lock_guard lock_guard{ctx.load_queue_lock};

        for (int song_id : ctx.unload_queue)
        {
            auto it = ctx.textures.song_map.find(song_id); 
            assert(it != ctx.textures.song_map.end());
            delete_texture(ctx.textures.song_map[song_id]);
            ctx.textures.song_map.erase(it);
        }
        ctx.unload_queue.clear();
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

    //static const ImWchar icons_ranges[] = { 0xf000, 0xf3ff, 0 }; // Will not be copied by AddFont* so keep in scope.
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);

    mp_ctx.song_constructor_callback = 
        [](Song* song) -> void
        {
            std::lock_guard lock{ctx.load_queue_lock};
            ctx.load_queue.push_back(std::make_pair(song->id, song->path));
        };

    mp_ctx.songs.set_destructor_callback(
        [](Song* song) -> void
        {
            std::lock_guard lock{ctx.unload_queue_lock};
            ctx.unload_queue.push_back(song->id);
        });
}

void ui_cleanup()
{
    cleanup_textures();
    glDeleteFramebuffers(1, &ctx.textures.fbo);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();

    SPDLOG_INFO("UI cleaned up");
}

static void draw_left_side()
{
    const ImVec2 size = ImVec2(LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE);
    if (mp_ctx.current_song != nullptr && ctx.textures.song_map.find(mp_ctx.current_song->id) != ctx.textures.song_map.end())
    {
        GLTexture2 tex = get_texture_from_slot_idx(ctx.textures.song_map[mp_ctx.current_song->id]);
        ImGui::ImageWithBg(tex.id, size, tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    else
    {
        ImGui::ImageWithBg(ctx.textures.default_album_art.id, size);
    }

    if (ImGui::Button("Skip", ImVec2(100, 30)) || (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_S)))
    {
        mp_queue_skip();
    }

    bool shuffle_copy = mp_ctx.shuffle;
    if (ImGui::Checkbox("Shuffle", &shuffle_copy))
        mp_toggle_shuffle();

    bool autoplay = mp_ctx.autoplay;
    if (ImGui::Checkbox("Autoplay", &autoplay))
        mp_toggle_autoplay();

    if (ImGui::SliderFloat("Volume", &mp_ctx.volume, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_None))
        mp_update_volume();

    char cursor_str[256];
    int cursor = static_cast<int>(mp_ctx.current_song_cursor);
    int length = static_cast<int>(mp_ctx.current_song_length);
    snprintf(cursor_str, sizeof(cursor_str), "%d:%02d / %d:%02d", cursor / 60, cursor % 60, length / 60, length % 60);
    if (ImGui::SliderFloat("Cursor", &mp_ctx.current_song_cursor, 0.0f, mp_ctx.current_song_length, cursor_str, ImGuiSliderFlags_None))
        mp_update_cursor();

    const char* loop_enum[] = {"none", "group", "track"};
    ImGui::Combo("combo", &mp_ctx.loop_mode, loop_enum, std::size(loop_enum));

    bool key_pressed = !ImGui::GetIO().WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_F9));
    if (ImGui::Button("Pause/Resume") || key_pressed)
        mp_pause_or_resume();

    if (ImGui::Button("Clear Queue"))
        mp_queue_clear();

    if (!mp_ctx.current_song) {
        ImGui::Text("No song playing");
    } else {
        ImGui::Text("Name: %s", mp_ctx.current_song->title.c_str());
        //ImGui::Text("Artist: %s", mp_ctx.current_song.artist.c_str());
        //ImGui::Text("Track: %s", mp_ctx.current_song.track.c_str());
    }

    if (mp_ctx.playing_group) {
        if (mp_ctx.group_is_album) {
            LruCacheRef<Album> album = mp_get_album(mp_ctx.group_id);
            ImGui::Text("Playing: %s", album->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album = std::move(album);
            }
        } else {
            LruCacheRef<Playlist> playlist  = mp_get_playlist(mp_ctx.group_id);
            ImGui::Text("Playing: %s", playlist->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                ctx.center = SHOW_CENTER_PLAYLIST;
                ctx.open_playlist = std::move(playlist);
            }
        }
    }

    if (ImGui::BeginTable("Queue", 1, ImGuiTableFlags_None))
    {
        ImGui::TableSetupColumn("Queue", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        for (const LruCacheRef<Song> & song : mp_ctx.queue)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        for (const auto& [song, track] : mp_ctx.group_queue)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        for (const LruCacheRef<Song>& song : mp_ctx.autoplay_queue)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Create Playlist"))
    {
        ctx.center = SHOW_CENTER_PLAYLIST;
        ctx.open_playlist = mp_create_playlist();
    }

    if (ImGui::BeginTable("Playlists", 2, ImGuiTableFlags_None))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        //for (const LruCacheRef<Playlist>& playlist : mp_ctx.playlists)
        //{
        //    ImGui::PushID(playlist.id);
        //    ImGui::TableNextColumn();
        //    if (ImGui::Button("Show")) {
        //        ctx.center = SHOW_CENTER_PLAYLIST;
        //        ctx.open_playlist_id = playlist.id;
        //    }
        //    ImGui::TableNextColumn();
        //    ImGui::Text("%s", playlist.name.c_str());
        //    ImGui::PopID();
        //}
        ImGui::EndTable();
    }
}

static void draw_search_results()
{
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("All Songs", 4, flags, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (LruCacheRef<Song>& song : mp_ctx.search_result)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(song->id);
            if (ImGui::Button("Play"))
                mp_play_song(song->id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song->id);
            ImGui::TableNextColumn();

            GLTexture2 tex;
            if (ctx.textures.song_map.find(song->id) != ctx.textures.song_map.end())
                tex = get_texture_from_slot_idx(ctx.textures.song_map[song->id]);
            else
            {
                tex = {
                    .id = ctx.textures.default_album_art.id,
                    .uv0 = ImVec2(0.0f, 0.0f),
                    .uv1 = ImVec2(1.0f, 1.0f),
                };
            }
            ImGui::ImageWithBg(tex.id, ImVec2(SMALL_COVER_ART_SIZE, SMALL_COVER_ART_SIZE), tex.uv0, tex.uv1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
            LruCacheRef<Artist> artist = mp_get_artist_from_song(song->id);
            if (artist)
                ImGui::Text("%s", artist->name.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album = mp_get_album_from_song(song->id);
            }
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                //for (const Playlist& playlist : mp_ctx.playlists)
                //{
                //    ImGui::PushID(playlist.id);
                //    if (ImGui::Button(playlist.name.c_str()))
                //        mp_add_song_id_to_playlist_id(song->id, playlist.id);
                //    ImGui::PopID();
                //}
                if (ImGui::Button("Create Playlist"))
                {
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist = mp_create_playlist();
                    mp_add_song_to_playlist(song->id, ctx.open_playlist->id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_album_info()
{
    if (ctx.open_album == nullptr)
        return;

    LruCacheRef<std::vector<SongTrackID>> tracks = mp_get_songs_from_album(ctx.open_album->id);
    if (tracks->size() == 0)
        return;

    LruCacheRef<Album> album = mp_get_album(ctx.open_album->id);
    LruCacheRef<Artist> artist = mp_get_artist_from_album(ctx.open_album->id);

    ImGui::ImageWithBg(ctx.textures.default_album_art.id, ImVec2(LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    {
        ImGui::BeginChild("album_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(4.0f); 
        ImGui::Text("%s", album->name.c_str());
        ImGui::SetWindowFontScale(2.0f); 
        char artist_buf[256];
        if (artist)
            snprintf(artist_buf, sizeof(artist_buf), "%s##", artist->name.c_str());
        else
            snprintf(artist_buf, sizeof(artist_buf), "##");
        if (ImGui::Button(artist_buf))
        {
            ctx.center = SHOW_CENTER_ARTIST;
            ctx.open_artist = std::move(artist);
        }
        ImGui::SetWindowFontScale(1.0f); 
        if (ImGui::Button("Queue"))
            for (const auto& [song_id, track] : *tracks)
                mp_queue_song(song_id);
        if (ImGui::Button("Play"))
            mp_play_album(album->id);
        if (ImGui::Button("Add To Playlist"))
            ImGui::OpenPopup("add_to_playlist_popup");
        char length_str[256];
        int length = static_cast<int>(album->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);
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
                ctx.center = SHOW_CENTER_PLAYLIST;
                ctx.open_playlist = mp_create_playlist();
                mp_add_album_to_playlist(album->id, ctx.open_playlist->id);
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    if (ImGui::BeginTable("Nested ALbum Songs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int id = 0;
        for (const auto& [song_id, track] : *tracks)
        {
            LruCacheRef<Song> song = mp_get_song(song_id);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 40.0f);
            ImGui::TableNextColumn();
            ImGui::PushID(id++);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::Text("%d", track);
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_playlist_info()
{
    LruCacheRef<std::vector<SongTrackID>> tracks = mp_get_songs_from_playlist(ctx.open_playlist->id);

    LruCacheRef<Playlist>& playlist = ctx.open_playlist;

    GLuint texture = ctx.textures.default_album_art.id;

    ImGui::ImageWithBg(texture, ImVec2(LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    {
        ImGui::BeginChild("playlist_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(4.0f); 
        ImGui::Text("%s", playlist->name.c_str());
        ImGui::SetWindowFontScale(1.0f); 
        static char playlist_name[256];
        if (ImGui::Button("Change Name")) {
            std::strncpy(playlist_name, playlist->name.c_str(), sizeof(playlist_name));
            ImGui::OpenPopup("change_playlist_name");
        }

        if (ImGui::Button("Queue"))
            for (const auto& [song_id, track] : *tracks)
                mp_queue_song(song_id);

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

    if (tracks->size() == 0) {
        ImGui::Text("No Songs");
        return;
    }

    if (ImGui::BeginTable("Nested ALbum Songs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int id = 0;
        for (const auto& [song_id, track] : *tracks)
        {
            LruCacheRef<Song> song = mp_get_song(song_id);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 40.0f);
            ImGui::TableNextColumn();
            ImGui::PushID(id++);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::Text("%d", track);
            ImGui::TableNextColumn();
            LruCacheRef<Album> album = mp_get_album_from_song(song_id);
            LruCacheRef<Artist> artist = mp_get_artist_from_song(song_id);
            ImGui::Text("%s", song->title.c_str());
            char album_name[256];
            if (album) {
                snprintf(album_name, sizeof(album_name), "%s", album->name.c_str());
                if (ImGui::Button(album_name))
                {
                    ctx.center = SHOW_CENTER_ALBUM;
                    ctx.open_album = std::move(album);
                }
            }
            if (artist) {
                ImGui::Text("%s", artist->name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_artist_info()
{
    LruCacheRef<Artist>& artist = ctx.open_artist;
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
        for (int song_id : *song_ids)
        {
            LruCacheRef<Song> song = mp_get_song(song_id);
            ImGui::TableNextColumn();
            ImGui::PushID(song_id);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::ImageWithBg(ctx.textures.default_album_art.id, ImVec2(50, 50), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());

            LruCacheRef<Artist> artist = mp_get_artist_from_song(song_id);
            if (artist != nullptr) {
                char artist_str[256];
                snprintf(artist_str, sizeof(artist_str), "%s", artist->name.c_str());
                if (ImGui::Button(artist_str))
                {
                    ctx.center = SHOW_CENTER_ARTIST;
                    ctx.open_artist = std::move(artist);
                }
            }

            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album = mp_get_album_from_song(song_id);
            }
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
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist = mp_create_playlist();
                    mp_add_song_to_playlist(song_id, ctx.open_playlist->id);
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
        for (int album_id : *album_ids)
        {
            LruCacheRef<Album> album = mp_get_album(album_id);
            ImGui::PushID(album_id);
            ImGui::TableNextColumn();
            ImGui::Text("tmp");
            ImGui::TableNextColumn();
            ImGui::Text("%s", album->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) 
            {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album = mp_get_album(album_id);
            }
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
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist = mp_create_playlist();
                    mp_add_album_to_playlist(album->id, ctx.open_playlist->id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_center()
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
    ImGui::SameLine();
    if (ImGui::Button("Add Folder")) 
    {
        const std::string title {"Select Any Directory"};
        const std::string default_path = pfd::path::home();
        const std::string folder_path = pfd::select_folder(title, default_path).result();
        mp_recursive_add_songs(folder_path);
    }
    ImGui::SameLine();
    ImGui::Text("Search");
    static char search_query[256];
    ImGui::SameLine();
    if (ImGui::InputTextWithHint("input text (w/ hint)", "Search...", search_query, sizeof(search_query))) 
    {
        mp_search_songs(search_query);
        ctx.center = SHOW_CENTER_SEARCH_RESULT;
    }
    if (ImGui::IsItemClicked())
        snprintf(search_query, sizeof(search_query), "");

    if (ctx.center == SHOW_CENTER_ALBUM)
        draw_album_info();
    else if (ctx.center == SHOW_CENTER_PLAYLIST)
        draw_playlist_info();
    else if (ctx.center == SHOW_CENTER_ARTIST)
        draw_artist_info();
    else if (ctx.center == SHOW_CENTER_SEARCH_RESULT)
        draw_search_results();
}

void draw_right_side()
{
    if (ImGui::Button("Close"))
    {
        ctx.right_side = SHOW_RIGHT_NONE;
        return;
    }
    LruCacheRef<Song> song = mp_get_song(ctx.right_side_song_id);
    if (ImGui::ImageButton("Press", ctx.textures.default_album_art.id, ImVec2(LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE)))
    {
        const std::string title {"Choose files to read"};
        const std::string default_path = pfd::path::home();
        const std::vector<std::string> filters {"All Files", "*"};
        std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters).result();
        if (song_paths.size() > 0)
            mp_song_front_cover_update(song->id, song_paths.front());
    }

    //ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInput, false);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;

    ImGui::Text("Title: ");
    ImGui::SameLine();
    ctx.right_side_changed |= ImGui::InputText("aa", ctx.right_side_song_title, STRING_LENGTH, flags);

    ImGui::Text("Artist: ");
    ImGui::SameLine();
    ctx.right_side_changed |= ImGui::InputText("bb", ctx.right_side_song_artist, STRING_LENGTH, flags);

    ImGui::Text("Album: ");
    ImGui::SameLine();
    ctx.right_side_changed |= ImGui::InputText("cc", ctx.right_side_song_album, STRING_LENGTH, flags);

    if (ImGui::Button("Save"))
    {
        mp_song_update(song->id, ctx.right_side_song_title, ctx.right_side_song_artist, ctx.right_side_song_album, nullptr);
        ctx.right_side_changed = false;
    }
    if (ctx.right_side_changed)
    {
        ImGui::SameLine();
        ImGui::Text("Unsaved Changes");
    }
}

static void draw_imgui()
{
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ctx.show_demo_window)
        ImGui::ShowDemoWindow(&ctx.show_demo_window);

    static bool window_open = true;
    ImGuiWindowFlags window_flags{};
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("UMP", &window_open, window_flags);

    bool right_side_open = ctx.right_side != SHOW_RIGHT_NONE;
    if (ImGui::BeginTable("view", 2 + right_side_open, ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort);

        ImGui::TableNextColumn();
        draw_left_side();
        ImGui::TableNextColumn();
        draw_center();
        if (right_side_open)
        {
            ImGui::TableNextColumn();
            draw_right_side();
        }
        ImGui::EndTable();
    }

    //draw_left_side();
    //ImGui::SameLine();
    //draw_center();
    //ImGui::SameLine();
    //draw_right_side();

    ImGui::End();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ui_loop()
{
    while (!glfwWindowShouldClose(ctx.window))
    {
        mp_update();
        glfwPollEvents();
        if (glfwGetWindowAttrib(ctx.window, GLFW_ICONIFIED) != 0)
            continue;

        int display_w, display_h;
        glfwGetFramebufferSize(ctx.window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        update_textures();
        draw_imgui();

        glfwSwapBuffers(ctx.window);
    }
}

