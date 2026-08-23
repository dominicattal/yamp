#include "mp.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <codecvt>
#include <cassert>
#include <miniaudio.h>
#include <id3v2lib.h>
#include <sqlite3.h>
#include <utf8.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <stb_image.h>

struct MPContextInternal {
    ma_engine engine;
    ma_sound current_song_sound;
    bool current_song_loaded;
    sqlite3* db;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg;
    sqlite3_exec(ctx.db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("SQLite3 error: %s", error_msg);
        sqlite3_free(error_msg);
    }
}

static int song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int id = std::atoi(values[0]);
    std::string title = values[1];
    std::string path = values[2];
    Song& song = mp_ctx.songs.emplace_back(title, path, id);
    //Song& song = mp_ctx.songs.emplace(std::make_pair(id, Song{title, path, id}));
    if (mp_ctx.song_callback)
        mp_ctx.song_callback(&song);
    return 0;
}

static int album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int album_id = std::atoi(values[0]);
    mp_ctx.albums.emplace_back(values[1], album_id);
    return 0;
}

static int artist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int artist_id = std::atoi(values[0]);
    mp_ctx.artists.emplace_back(values[1], artist_id);
    return 0;
}

static int playlist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int playlist_id = std::atoi(values[0]);
    mp_ctx.playlists.emplace_back(values[1], playlist_id);
    return 0;
}

static int album_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int album_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.album_songs.emplace_back(album_id, song_id, track);
    return 0;
}

static int artist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    mp_ctx.artist_songs.emplace_back(artist_id, song_id);
    return 0;
}

static int artist_album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int album_id = std::atoi(values[1]);
    mp_ctx.artist_albums.emplace_back(artist_id, album_id);
    return 0;
}

static int playlist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int playlist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.playlist_songs.emplace_back(playlist_id, song_id, track);
    return 0;
}

static void db_init()
{
    sqlite3_open("build/yamp.db", &ctx.db);

    execute_file("assets/sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(*) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        puts("failed");
        sqlite3_finalize(stmt);
        return;
    }
    int num_rows = sqlite3_column_int(stmt, 0);
    SPDLOG_INFO("num rows: {}", num_rows);
    // load the example data
    if (num_rows == 0) {
        //execute_file("assets/sql/insert.sql");
    }
    sqlite3_finalize(stmt);
}

void mp_init()
{
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    sqlite3_exec(ctx.db, "SELECT * FROM Songs", song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Albums", album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Artists", artist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Playlists", playlist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM AlbumSong", album_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistAlbum", artist_album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistSong", artist_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM PlaylistSong", playlist_song_callback, NULL, NULL);
}

void mp_cleanup()
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_engine_uninit(&ctx.engine);
    sqlite3_close(ctx.db);
    SPDLOG_INFO("MP cleaned up");
}

static std::string read_text_frame(ID3v2_TextFrame* frame)
{
    constexpr int ASCII = 0;
    constexpr int UNICODE_UTF16_WITH_BOM = 1;
    constexpr int UNICODE_UTF16_WITHOUT_BOM = 2;
    constexpr int UNICODE_UTF8 = 3;
    const char* text = frame->data->text;
    switch (frame->data->encoding) {
        case ASCII:
        case UNICODE_UTF8:
            return text;
        case UNICODE_UTF16_WITH_BOM: {
            unsigned int bom = (static_cast<unsigned char>(text[1])<<8) | static_cast<unsigned char>(text[0]);
            assert(bom == 0xFEFF);
            std::u16string str(reinterpret_cast<const char16_t*>(text + 2), (frame->data->size - 4) / 2);
            return utf8::utf16to8(str);
        }
        case UNICODE_UTF16_WITHOUT_BOM: {
            std::u16string str(reinterpret_cast<const char16_t*>(text), (frame->data->size - 2) / 2);
            return utf8::utf16to8(str);
        }
    }
    SPDLOG_INFO("Unrecognized encoding {}", frame->data->encoding);
    return "";
}

void mp_add_song(const std::string& song_path)
{
    ID3v2_TextFrame* frame;
    std::string title{};
    std::string artist_name{};
    std::string album_name{};
    std::string path{song_path};
    int track{};
    ID3v2_Tag* tag = ID3v2_read_tag(song_path.c_str());
    if (!tag) {
        puts("Could not read tag");
        return;
    }

    frame = ID3v2_Tag_get_title_frame(tag);
    if (frame) {
        title = read_text_frame(frame);
    } else {
        std::filesystem::path path{song_path};
        title = path.stem().string();
    }

    frame = ID3v2_Tag_get_artist_frame(tag);
    if (frame)
        artist_name = read_text_frame(frame);

    frame = ID3v2_Tag_get_track_frame(tag);
    if (frame)
        track = std::stoi(read_text_frame(frame));

    frame = ID3v2_Tag_get_album_frame(tag);
    if (frame)
        album_name = read_text_frame(frame);

    SPDLOG_INFO(track);

    sqlite3_stmt* stmt;
    const char* query;
    query = "INSERT INTO Songs (title, path) VALUES (?1, ?2);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Songs WHERE title=?1 ORDER BY id DESC LIMIT 1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    Song& song = mp_ctx.songs.emplace_back(title, path, song_id);
    if (mp_ctx.song_callback)
        mp_ctx.song_callback(&song);

    SPDLOG_INFO("Created song {} {}", song_id, title);

    int album_id{}, artist_id{};
    if (album_name.size() > 0) {
        query = "SELECT id FROM Albums WHERE name=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO Albums (name) VALUES (?1)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            query = "SELECT id FROM Albums WHERE name=?1";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            album_id = sqlite3_column_int(stmt, 0);
            mp_ctx.albums.emplace_back(album_name, album_id);
            sqlite3_finalize(stmt);

            SPDLOG_INFO("Created album {} {}", album_id, album_name);
        } else {
            album_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        query = "INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?1, ?2, ?3)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, album_id);
        sqlite3_bind_int(stmt, 2, song_id);
        sqlite3_bind_int(stmt, 3, track);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        mp_ctx.album_songs.emplace_back(album_id, song_id, track);
    }

    if (artist_name.size() > 0) {
        query = "SELECT id FROM Artists WHERE name=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO Artists (name) VALUES (?1)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            query = "SELECT id FROM Artists WHERE name=?1";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            artist_id = sqlite3_column_int(stmt, 0);
            mp_ctx.artists.emplace_back(artist_name, artist_id);
            sqlite3_finalize(stmt);

            SPDLOG_INFO("Created artist {} {}", artist_id, artist_name);
        } else {
            artist_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        query = "INSERT INTO ArtistSong (artist_id, song_id) VALUES (?1, ?2)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_bind_int(stmt, 2, song_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        mp_ctx.artist_songs.emplace_back(artist_id, song_id);
    }

    if (artist_name.size() > 0 && album_name.size() > 0) {
        assert(artist_id > 0);
        assert(album_id > 0);
        query = "SELECT * FROM ArtistAlbum WHERE artist_id=?1 AND album_id=?2";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_bind_int(stmt, 2, album_id);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, artist_id);
            sqlite3_bind_int(stmt, 2, album_id);
            sqlite3_step(stmt);
            SPDLOG_INFO("Associated artist={} album={}", artist_id, album_id);
            mp_ctx.artist_albums.emplace_back(artist_id, album_id);
        }
        sqlite3_finalize(stmt);
    }

    ID3v2_Tag_free(tag);
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_add_song(song_path);
}

void mp_play_song(const Song* song)
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_sound_init_from_file(&ctx.engine, song->path.c_str(), 0, NULL, NULL, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);
    ctx.current_song_loaded = true;
    mp_ctx.current_song = song;
}

void mp_queue_song(const Song* song)
{
    mp_ctx.queue.push_back(song);
}

void mp_queue_songs(const std::vector<const Song*> songs)
{
    for (const Song* song : songs)
        mp_queue_song(song);
}

FrontCover mp_song_front_cover_load(Song* song)
{
    FrontCover front_cover{};
    ID3v2_Tag* tag = ID3v2_read_tag(song->path.c_str());
    if (!tag)
        return front_cover;
    ID3v2_ApicFrame* apic_cover = ID3v2_Tag_get_album_cover_frame(tag);
    if (!apic_cover)
        return front_cover;
    unsigned char* apic_data = (unsigned char*)apic_cover->data->data;
    int picture_size = apic_cover->data->picture_size;
    int num_channels;
    unsigned char* raw_data = stbi_load_from_memory(apic_data, picture_size, &front_cover.width, &front_cover.height, &num_channels, 4);
    front_cover.data = raw_data;
    ID3v2_Tag_free(tag);
    return front_cover;
}

void mp_song_front_cover_free(FrontCover* front_cover)
{
    if (front_cover->data)
        stbi_image_free(front_cover->data);
}

const Song* mp_get_song_from_id(int id)
{
    for (const Song& song : mp_ctx.songs)
        if (song.id == id)
            return &song;
    return nullptr;
}

const Album* mp_get_album_from_id(int id)
{
    for (const Album& album : mp_ctx.albums)
        if (album.id == id)
            return &album;
    return nullptr;
}

const Artist* mp_get_artist_from_id(int id)
{
    for (const Artist& artist : mp_ctx.artists)
        if (artist.id == id)
            return &artist;
    return nullptr;
}

int mp_get_artist_id_from_song_id(int song_id)
{
    for (const ArtistSong& artist_song : mp_ctx.artist_songs)
        if (artist_song.song_id == song_id)
            return artist_song.artist_id;
    return -1;
}

int mp_get_artist_id_from_album_id(int album_id)
{
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.album_id == album_id)
            return artist_album.artist_id;
    return -1;
}

std::vector<SongTrack> mp_get_song_ids_from_album_id(int album_id)
{
    std::vector<SongTrack> result{};
    for (const AlbumSong& album_song : mp_ctx.album_songs) {
        if (album_song.album_id == album_id)
            result.emplace_back(album_song.song_id, album_song.track);
    }
    std::sort(result.begin(), result.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                return pair1.track < pair2.track;
            });
    return result;
}

std::vector<int> mp_get_album_ids_from_artist_id(int artist_id)
{
    std::vector<int> result{};
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.artist_id == artist_id)
            result.push_back(artist_album.album_id);
    return result;
}

int mp_get_album_id_from_song_id(int song_id)
{
    for (const AlbumSong& album_song : mp_ctx.album_songs)
        if (album_song.song_id == song_id)
            return album_song.album_id;
    return -1;
}

int mp_get_album_id_from_artist_id(int artist_id)
{
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.artist_id == artist_id)
            return artist_album.album_id;
    return -1;
}

void mp_queue_skip()
{
    if (mp_ctx.queue.size() == 0) {
        if (ctx.current_song_loaded) {
            mp_ctx.current_song = nullptr;
            ma_sound_uninit(&ctx.current_song_sound);
            ctx.current_song_loaded = false;
        }
        return;
    }

    const Song* song = mp_ctx.queue.front();
    mp_ctx.queue.pop_front();
    mp_play_song(song);
}
