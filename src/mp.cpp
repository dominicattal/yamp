#include "mp.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <codecvt>
#include <cassert>
#include <sqlite3.h>
#include <miniaudio.h>
#include <random>
#include <fileref.h>
#include <tag.h>
#include <tstringlist.h>
#include <utf8.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <stb_image.h>

struct MPContextInternal {
    std::mt19937 mt;
    sqlite3* db;
    ma_engine engine;
    ma_sound current_song_sound;
    bool paused;
    bool current_song_loaded;
    bool song_ended;
};

MPContext mp_ctx;
static MPContextInternal ctx;

// -----------------------
// DB DECLARATIONS
// -----------------------

// Create
static void db_create_song(const char* title, const char* song_path, double song_length);
static void db_create_album(const char* name);
static void db_create_artist(const char* name);
static void db_create_playlist(const char* name);
static void db_create_artist_song(int artist_id, int song_id);
static void db_create_album_song(int album_id, int song_id, int track);
static void db_create_playlist_song(int playlist_id, int song_id, int track);
static void db_create_artist_album(int artist_id, int album_id);

// Read
static int db_get_song_id(const char* song_path);
static int db_get_album_id(const char* name);
static int db_get_artist_id(const char* name);
static int db_get_playlist_id(const char* name);
static Song db_get_song_info(int song_id);
static Album db_get_album_info(int album_id);
static Artist db_get_artist_info(int album_id);
static Playlist db_get_playlist_info(int album_id);
static int db_get_album_from_song(int song_id);
static std::vector<SongTrackID> db_get_songs_from_album(int album_id);
static bool db_exists_artist_album(int artist_id, int album_id);

// Update
[[maybe_unused]] static void db_update_song(int song_id, const char* new_title, const char* new_song_path, double new_song_length);
[[maybe_unused]] static void db_update_song_artist(int song_id, const char* artist);
[[maybe_unused]] static void db_update_playlist(int playlist_id, const char* new_name);

// Delete
static void db_delete_artist(int artist_id);
[[maybe_unused]] static void db_delete_album(int album_id);
[[maybe_unused]] static void db_delete_song(int song_id);

// -----------------------
// DB DEFINITIONS
// -----------------------

static int db_get_song_id(const char* song_path)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Songs WHERE path=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, song_path, -1, SQLITE_TRANSIENT);
    int res = sqlite3_step(stmt);
    int song_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return song_id;
}

static Song db_get_song_info(int song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT title, path, length FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const float length = static_cast<float>(sqlite3_column_double(stmt, 2));
    Song song{title, path, song_id, length};
    sqlite3_finalize(stmt);
    return song;

    //query = "SELECT artist_id FROM ArtistSong WHERE song_id=?1";
    //sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    //sqlite3_bind_int(stmt, 1, song_id);
    //res = sqlite3_step(stmt);
    //if (res == SQLITE_ROW)
    //    song.artist_id = sqlite3_column_int(stmt, 0);
    //sqlite3_finalize(stmt);

    //query = "SELECT album_id FROM AlbumSong WHERE song_id=?1";
    //sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    //sqlite3_bind_int(stmt, 1, song_id);
    //res = sqlite3_step(stmt);
    //if (res == SQLITE_ROW)
    //    song.album_id = sqlite3_column_int(stmt, 0);

    //sqlite3_finalize(stmt);

    //return song;
}

static Album db_get_album_info(int album_id)
{
    Album album{};
    album.id = album_id;

    sqlite3_stmt* stmt;
    const char* query = "SELECT name FROM Albums WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    album.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    auto songs = mp_get_songs_from_album(album_id);
    for (SongTrackID& song_track : *songs) {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        album.length += song->length;
    }

    sqlite3_finalize(stmt);
    return album;
}

static Artist db_get_artist_info(int artist_id)
{
    Artist artist{};
    artist.id = artist_id;
    
    sqlite3_stmt* stmt;
    const char* query = "SELECT name FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    artist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    return artist;
}

static Playlist db_get_playlist_info(int playlist_id)
{
    Playlist playlist{};
    playlist.id = playlist_id;

    sqlite3_stmt* stmt;
    const char* query = "SELECT name FROM Playlists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    playlist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    auto songs = mp_get_songs_from_playlist(playlist_id);
    for (SongTrackID& song_track : *songs) {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        playlist.length += song->length;
    }

    sqlite3_finalize(stmt);
    return playlist;
}

static int db_get_album_from_song(int song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM AlbumSong WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    int album_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return album_id;
}

static std::vector<SongTrackID> db_get_songs_from_album(int album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id, track FROM AlbumSong WHERE album_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);

    std::vector<SongTrackID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int song_id = sqlite3_column_int(stmt, 0);
        int track = sqlite3_column_int(stmt, 1);
        songs.emplace_back(song_id, track);
    }

    sqlite3_finalize(stmt);
    return songs;
}

static int db_get_artist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Artists WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int res = sqlite3_step(stmt);
    int artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return artist_id;
}

static void db_create_artist(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Artists (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_create_song(const char* title, const char* song_path, double song_length)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Songs (title, path, length) VALUES (?1, ?2, ?3);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, song_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, song_length);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (res == SQLITE_CONSTRAINT)
        SPDLOG_WARN("song title={} path={} exists in db already", title, song_path);
}

static void db_update_song(int song_id, const char* new_title, const char* new_song_path, double new_song_length)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET title=?1, path=?2, length=?3 WHERE id=?4";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, new_song_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, new_song_length);
    sqlite3_bind_int(stmt, 4, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_delete_artist(int artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_create_artist_song(int artist_id, int song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistSong (artist_id, song_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_update_song_artist(int song_id, const char* artist)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistSong WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    int artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    if (artist_id != -1)
    {
        query = "DELETE FROM ArtistSong WHERE song_id=?1 AND artist_id=?2";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, song_id);
        sqlite3_bind_int(stmt, 2, artist_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        query = "SELECT COUNT(artist_id) FROM ArtistSong NATURAL JOIN ArtistAlbum WHERE artist_id=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (count == 0)
            db_delete_artist(artist_id);
    }

    artist_id = db_get_artist_id(artist);
    if (artist_id == -1)
    {
        db_create_artist(artist);
        artist_id = db_get_artist_id(artist);
    }
    db_create_artist_song(artist_id, song_id);
}

[[maybe_unused]] static void db_delete_album(int album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Albums WHERE album_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

[[maybe_unused]] static void db_delete_song(int song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Songs WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static int db_get_album_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Albums WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int res = sqlite3_step(stmt);
    int album_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return album_id;
}

static void db_create_album(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Albums (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_create_album_song(int album_id, int song_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static int db_get_playlist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Playlists WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int res = sqlite3_step(stmt);
    int playlist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return playlist_id;
}

static void db_create_playlist(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Playlists (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void db_update_playlist(int playlist_id, const char* new_name)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Playlists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

[[maybe_unused]] static void db_create_playlist_song(int playlist_id, int song_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO PlaylistSong (playlist_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static bool db_exists_artist_album(int artist_id, int album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM ArtistAlbum WHERE artist_id=?1 AND album_id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, album_id);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return res == SQLITE_ROW;
}

static void db_create_artist_album(int artist_id, int album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// -----------------------
// DB API END
// -----------------------

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg{};
    sqlite3_exec(ctx.db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("SQLite3 error: {}", error_msg);
        sqlite3_free(error_msg);
    }
}

static void db_init()
{
    sqlite3_open("build/yamp.db", &ctx.db);
    execute_file("assets/sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
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
    ctx.mt.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    mp_ctx.volume = 1.0f;
    mp_ctx.shuffle = true;
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);
}

void mp_cleanup()
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_engine_uninit(&ctx.engine);
    sqlite3_close(ctx.db);
    SPDLOG_INFO("MP cleaned up");
}

void mp_update()
{
    if (mp_ctx.current_song != nullptr) {
        ma_sound_get_cursor_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_cursor);
    }
    if (ctx.song_ended) {
        mp_queue_skip();
        ctx.song_ended = false;
    }
}

void mp_add_song(const std::string& song_path)
{
    TagLib::FileRef mp3_file_ref(song_path.c_str());

    if (mp3_file_ref.isNull() || !mp3_file_ref.tag()) {
        SPDLOG_ERROR("Could not read {}", song_path);
        return;
    }

    TagLib::Tag* tag = mp3_file_ref.tag();
    assert(tag);
    TagLib::AudioProperties* properties = mp3_file_ref.audioProperties();
    assert(properties);
    std::string title = tag->title().to8Bit(true);
    std::string album_name = tag->album().to8Bit(true);
    std::string artist_name = tag->artist().to8Bit(true);
    double song_length = properties->lengthInMilliseconds() / 1000.0;
    int track = tag->track();

    int song_id = db_get_song_id(song_path.c_str());
    if (song_id != -1)
    {
        SPDLOG_WARN("Song id %d already exists", song_id);
        return;
    }

    db_create_song(title.c_str(), song_path.c_str(), song_length);
    song_id = db_get_song_id(song_path.c_str());

    SPDLOG_INFO("Created song {} {}", song_id, title);

    int album_id{}, artist_id{};

    if (album_name.size() > 0) 
    {
        album_id = db_get_album_id(album_name.c_str());
        if (album_id == -1) 
        {
            db_create_album(album_name.c_str());
            album_id = db_get_album_id(album_name.c_str());
        }
        db_create_album_song(album_id, song_id, track);
    }

    if (artist_name.size() > 0) 
    {
        artist_id = db_get_artist_id(artist_name.c_str());
        if (artist_id == -1)
        {
            db_create_artist(artist_name.c_str());
            artist_id = db_get_artist_id(artist_name.c_str());
        }
        db_create_artist_song(artist_id, song_id);
    }

    if (album_id != 0 && artist_id != 0 && !db_exists_artist_album(artist_id, album_id))
        db_create_artist_album(artist_id, album_id);
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_add_song(song_path);
}

void mp_recursive_add_songs(const std::string& folder_path)
{
    namespace fs = std::filesystem;

    if (!fs::exists(std::filesystem::path{folder_path}))
        return;

    fs::directory_options options = fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator entries = fs::recursive_directory_iterator(folder_path, options);
    for (auto const& dir_entry : entries)
        mp_add_song(dir_entry.path().string());
}

LruCacheRef<Playlist> mp_create_playlist()
{
    const char* playlist_name = "Unnamed Playlist";
    db_create_playlist(playlist_name);
    int playlist_id = db_get_playlist_id(playlist_name);
    return mp_get_playlist(playlist_id);
}

void mp_rename_playlist(int playlist_id, const char* new_playlist_name)
{
    db_update_playlist(playlist_id, new_playlist_name);
    LruCacheRef<Playlist> playlist = mp_get_playlist(playlist_id);
    playlist->name = new_playlist_name;
}

void mp_add_song_to_playlist(int song_id, int playlist_id)
{
    (void)song_id;
    (void)playlist_id;
    return;
    //LruCacheRef<std::vector<SongTrackID>> song_tracks = mp_get_songs_from_playlist(playlist_id);
    //int track = tracks->size() + 1;
    //db_create_playlist_song(playlist_id, song_id, track);
    //auto song_tracks = mp_ctx.playlist_songs.get(playlist_id);
    //LruCacheRef<Song> song = mp_get_song(song_id); 

    //if (song_tracks != nullptr)
    //    song_tracks->emplace_back(std::move(song), track);

    //if (auto playlist = mp_ctx.playlists.get(playlist_id); playlist)
    //    playlist->length += song->length;
}

void mp_add_album_to_playlist(int album_id, int playlist_id)
{
    LruCacheRef<std::vector<SongTrackID>> song_tracks = mp_get_songs_from_album(album_id);
    for (auto & [song_id, track] : *song_tracks) {
        LruCacheRef<Song> song = mp_get_song(song_id);
        mp_add_song_to_playlist(song->id, playlist_id);
    }
}

static void end_song_callback(void* user_data, ma_sound* sound)
{
    (void)user_data; (void)sound;
    ctx.song_ended = true;
}

void mp_play_song(int song_id)
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    LruCacheRef<Song> song = mp_get_song(song_id);
    ma_sound_config config = ma_sound_config_init();
    config.channelsIn = 1;
    config.pFilePath = song->path.c_str();
    config.endCallback = end_song_callback;
    ma_sound_init_ex(&ctx.engine, &config, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);
    ma_sound_get_length_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_length);
    ctx.current_song_loaded = true;
    mp_ctx.current_song = std::move(song);
}

void mp_queue_song(int song_id)
{
    LruCacheRef<Song> song = mp_get_song(song_id);
    mp_ctx.queue.push_back(std::move(song));
    if (mp_ctx.queue.size() == 1 && !ctx.current_song_loaded)
        mp_queue_skip();
}

void mp_pause_or_resume()
{
    if (!ctx.current_song_loaded)
        return;
    if (ctx.paused) {
        ctx.paused = false;
        ma_sound_start(&ctx.current_song_sound);
    } else {
        ctx.paused = true;
        ma_sound_stop(&ctx.current_song_sound);
    }
}

void mp_toggle_shuffle()
{
    mp_ctx.shuffle = !mp_ctx.shuffle;
    if (mp_ctx.playing_group) {
        if (mp_ctx.shuffle) {
            std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
        } else {
            std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                    return pair1.track < pair2.track;
                });
        }
    }
}

void mp_toggle_autoplay()
{
    if (mp_ctx.autoplay) {
        mp_ctx.autoplay_queue.clear();
    } else {
        //for (int i = 0; i < 10; i++) {
        //    size_t idx = ctx.mt() % mp_ctx.songs.size();
        //    LruCacheRef<Song> song = mp_get_song_from_id(idx);
        //    mp_ctx.autoplay_queue.push_back(song);
        //}
    }
    mp_ctx.autoplay = !mp_ctx.autoplay;
    if (mp_ctx.autoplay && mp_ctx.current_song == nullptr)
        mp_queue_skip();
}

void mp_update_volume()
{
    ma_sound_set_volume(&ctx.current_song_sound, mp_ctx.volume);
}

void mp_update_cursor()
{
    ma_sound_seek_to_second(&ctx.current_song_sound, mp_ctx.current_song_cursor);
}

static void sort_or_shuffle_group_queue()
{
    if (mp_ctx.shuffle) {
        std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
    } else {
        std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) 
            {
                return pair1.track < pair2.track;
            });
    }
}

static void add_playlist_songs_to_group_queue(int playlist_id)
{
    mp_ctx.group_queue.clear();
    LruCacheRef<std::vector<SongTrackID>> song_tracks = mp_get_songs_from_playlist(playlist_id);
    for (SongTrackID& song_track : *song_tracks)
    {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        mp_ctx.group_queue.emplace_back(std::move(song), song_track.track);
    }
    sort_or_shuffle_group_queue();
}

static void add_album_songs_to_group_queue(int album_id)
{
    mp_ctx.group_queue.clear();
    LruCacheRef<std::vector<SongTrackID>> song_tracks = mp_get_songs_from_album(album_id);
    for (SongTrackID& song_track : *song_tracks)
    {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        mp_ctx.group_queue.emplace_back(std::move(song), song_track.track);
    }
    sort_or_shuffle_group_queue();
}

void mp_play_playlist(int playlist_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = false;
    mp_ctx.group_id = playlist_id;
    mp_ctx.queue.clear();
    add_playlist_songs_to_group_queue(playlist_id);
    mp_queue_skip();
}

void mp_play_album(int album_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = true;
    mp_ctx.group_id = album_id;
    mp_ctx.queue.clear();
    add_album_songs_to_group_queue(album_id);
    mp_queue_skip();
}

FrontCover mp_song_front_cover_load(const std::string& cover_path)
{
    FrontCover front_cover{};
    TagLib::FileRef mp3_file_ref(cover_path.c_str());
    if (mp3_file_ref.isNull() || !mp3_file_ref.tag()) {
        SPDLOG_ERROR("Could not read {}", cover_path);
        return front_cover;
    }

    TagLib::List<TagLib::VariantMap> props = mp3_file_ref.complexProperties("PICTURE");
    if (props.isEmpty())
        return front_cover;

    const TagLib::VariantMap& map = props.front();
    if (map.contains("data")) 
    {
        int num_channels;
        const TagLib::ByteVector data = map["data"].toByteVector();
        front_cover.data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data.data()), data.size(), &front_cover.width, &front_cover.height, &num_channels, 4);
    }

    return front_cover;
}

void mp_song_front_cover_update(int song_id, const std::string& cover_path)
{
    LruCacheRef<Song> song = mp_get_song(song_id);
    std::ifstream img{cover_path.c_str(), std::ios::binary};
    std::vector<char> bytes{std::istreambuf_iterator<char>(img), std::istreambuf_iterator<char>()};
    
    TagLib::FileRef file(song->path.c_str());

    TagLib::VariantMap picture;
    picture["data"] = TagLib::ByteVector(bytes.data(), bytes.size());
    picture["mimeType"] = TagLib::String("image/jpeg");
    picture["pictureType"] = TagLib::String("Front Cover");
    picture["description"] = TagLib::String("");

    TagLib::List<TagLib::VariantMap> pictures;
    pictures.append(picture);

    file.setComplexProperties("PICTURE", pictures);
    file.save();
}

const std::vector<LruCacheRef<Song>>& mp_search_songs(const char* search_query)
{
    sqlite3_stmt* stmt;
    constexpr int limit = 50;
    const char* query = "SELECT id FROM Songs WHERE title LIKE ?1 LIMIT ?2";
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    std::vector<LruCacheRef<Song>> results{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int song_id = sqlite3_column_int(stmt, 0);
        LruCacheRef<Song> song = mp_get_song(song_id);
        assert(song != nullptr);
        results.push_back(std::move(song));
    }
    mp_ctx.search_result = std::move(results);
    sqlite3_finalize(stmt);
    return mp_ctx.search_result;
}

void mp_song_front_cover_free(FrontCover* front_cover)
{
    if (front_cover->data)
        stbi_image_free(front_cover->data);
}

void mp_song_update(int song_id, const char* title, const char* artist, const char* album, const char* cover_path)
{
    (void)song_id;
    (void)title;
    (void)artist;
    (void)album;
    (void)cover_path;
    //Song* song = mp_get_song_from_id(song_id);
    //int album_id = mp_get_album_id_from_song_id(song_id);
    //db_update_song(song_id, title, song->path.c_str(), song->length);

    //db_update_song_artist(song_id, artist);
    //db_update_song_album(song_id, album);

    //song->title = std::string{title};
    // change artistsong
    // change albumsong
    return;
}

LruCacheRef<Song> mp_get_song(int song_id)
{
    LruCacheRef<Song> song = mp_ctx.songs.get(song_id);
    if (song == nullptr) {
        song = mp_ctx.songs.put(song_id, db_get_song_info(song_id));
        if (mp_ctx.song_constructor_callback)
            mp_ctx.song_constructor_callback(song.get());
    }
    return song;
}

LruCacheRef<Album> mp_get_album(int album_id)
{
    LruCacheRef<Album> album = mp_ctx.albums.get(album_id);
    if (album == nullptr)
        album = mp_ctx.albums.put(album_id, db_get_album_info(album_id));
    return album;
}

LruCacheRef<Artist> mp_get_artist(int artist_id)
{
    LruCacheRef<Artist> artist = mp_ctx.artists.get(artist_id);
    if (artist == nullptr)
        artist = mp_ctx.artists.put(artist_id, db_get_artist_info(artist_id));
    return artist;
}

LruCacheRef<Playlist> mp_get_playlist(int playlist_id)
{
    LruCacheRef<Playlist> playlist = mp_ctx.playlists.get(playlist_id);
    if (playlist == nullptr)
        playlist = mp_ctx.playlists.put(playlist_id, db_get_playlist_info(playlist_id));
    return playlist;
}

LruCacheRef<Artist> mp_get_artist_from_song(int song_id)
{
    (void)song_id;
    return {};
}

LruCacheRef<Album> mp_get_album_from_song(int song_id)
{
    LruCacheRef<int> album_id = mp_ctx.song_album.get(song_id);
    if (album_id == nullptr)
        album_id = mp_ctx.song_album.put(song_id, db_get_album_from_song(song_id));
    return mp_get_album(*album_id);
}

LruCacheRef<Artist> mp_get_artist_from_album(int album_id)
{
    (void)album_id;
    return {};
}

LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_album(int album_id)
{
    LruCacheRef<std::vector<SongTrackID>> songs = mp_ctx.album_songs.get(album_id);
    if (songs == nullptr)
        songs = mp_ctx.album_songs.put(album_id, db_get_songs_from_album(album_id));
    return songs;
}

LruCacheRef<std::vector<SongTrackID>> mp_get_songs_from_playlist(int playlist_id)
{
    (void)playlist_id;
    return {};
}

LruCacheRef<std::vector<int>> mp_get_songs_from_artist(int artist_id)
{
    (void)artist_id;
    return {};
}

LruCacheRef<std::vector<int>> mp_get_albums_from_artist(int artist_id)
{
    (void)artist_id;
    return {};
}

static void play_next_group_song()
{
    if (mp_ctx.group_queue.size() == 0) 
    {
        if (mp_ctx.loop_mode == LOOP_NONE) {
            mp_ctx.playing_group = false;
            return;
        }

        if (mp_ctx.group_is_album)
            add_album_songs_to_group_queue(mp_ctx.group_id);
        else
            add_playlist_songs_to_group_queue(mp_ctx.group_id);

        if (mp_ctx.group_queue.size() == 0) {
            SPDLOG_WARN("Tried to play group with no songs");
            mp_ctx.playing_group = false;
            return;
        }
    }
    const int song_id = mp_ctx.group_queue.front().song->id;
    mp_ctx.group_queue.pop_front();
    mp_play_song(song_id);
}

void mp_queue_skip()
{
    if (mp_ctx.loop_mode == LOOP_TRACK) {
        mp_play_song(mp_ctx.current_song->id);
        return;
    }
    ctx.paused = false;
    if (mp_ctx.queue.size() == 0) {
        if (mp_ctx.playing_group) {
            play_next_group_song();
        } else if (mp_ctx.autoplay) {
            LruCacheRef<Song> song = std::move(mp_ctx.autoplay_queue.front());
            mp_ctx.autoplay_queue.pop_front();
            //size_t idx = ctx.mt() % mp_ctx.songs.size();
            size_t idx = 1;
            mp_ctx.autoplay_queue.push_back(mp_get_song(idx));
            mp_play_song(song->id);
        } else if (ctx.current_song_loaded) {
            mp_ctx.current_song = nullptr;
            ma_sound_uninit(&ctx.current_song_sound);
            ctx.current_song_loaded = false;
        }
    } else {
        LruCacheRef<Song> song = std::move(mp_ctx.queue.front());
        mp_ctx.queue.pop_front();
        mp_play_song(song->id);
    }
}

void mp_queue_clear()
{
    mp_ctx.queue.clear();
}
