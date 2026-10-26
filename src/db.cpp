#include "db.h"
#include "config.h"
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

#define MPUID_SHIFT 62
#define MPUID_ID_MASK ((1ULL<<MPUID_SHIFT)-1)
#define MPUID_TYPE_MASK (3ULL<<MPUID_SHIFT)

struct {
    sqlite3* db;
} ctx;

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

MPUID db_get_song_uid(SongID song_id)
{
    return (MPUID_SONG<<MPUID_SHIFT) | static_cast<MPUID>(song_id);
}

MPUID db_get_playlist_uid(PlaylistID playlist_id)
{
    return (MPUID_PLAYLIST<<MPUID_SHIFT) | static_cast<MPUID>(playlist_id);
}

MPUID db_get_album_uid(AlbumID album_id)
{
    return (MPUID_ALBUM<<MPUID_SHIFT) | static_cast<MPUID>(album_id);
}

MPUID db_get_artist_uid(ArtistID artist_id)
{
    return (MPUID_ARTIST<<MPUID_SHIFT) | static_cast<MPUID>(artist_id);
}

MPUID db_get_uid(int id, MPUIDEnum type)
{
    return (type<<MPUID_SHIFT) | static_cast<MPUID>(id);
}

MPUIDEnum db_get_uid_type(MPUID uid)
{
    return static_cast<MPUIDEnum>((uid & MPUID_TYPE_MASK)>>MPUID_SHIFT);
}

int db_get_id(MPUID uid)
{
    return uid & MPUID_ID_MASK;
}

void db_init()
{
    //sqlite3_open("build/yamp.db", &ctx.db);
    sqlite3_open_v2(
        config.db_path.c_str(),
        &ctx.db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    execute_file("assets/sql/schema.sql");

    SPDLOG_INFO("DB at {} loaded successfully", config.db_path);

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    [[maybe_unused]] int res = sqlite3_step(stmt);
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

void db_cleanup()
{
    int res = sqlite3_close(ctx.db);
    if (res == SQLITE_BUSY)
        SPDLOG_WARN("SQLite busy on cleanup");
    SPDLOG_INFO("MP cleaned up");
}

void db_switch(const std::string& db_path)
{
    sqlite3* new_db;
    int res = sqlite3_open_v2(
        db_path.c_str(),
        &new_db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX,
        nullptr
    );

    if (res != SQLITE_OK)
    {
        SPDLOG_ERROR("Could not change to db {}", db_path);
        return;
    }

    sqlite3_close(ctx.db);
    config.db_path = db_path;
    ctx.db = new_db;
    SPDLOG_INFO("Changed db to {}", db_path);
}

SongID db_get_song_id(const char* song_path)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Songs WHERE path=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, song_path, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    SongID song_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return song_id;
}

Song db_get_song_info(SongID song_id)
{
    Song song{};
    sqlite3_stmt* stmt;
    const char* query = "SELECT title, path, year, length, volume FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    song.id = song_id;
    song.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    song.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    song.year = sqlite3_column_int(stmt, 2);
    song.length = static_cast<float>(sqlite3_column_double(stmt, 3));
    song.volume = sqlite3_column_double(stmt, 4);
    sqlite3_finalize(stmt);
    return song;
}

Art db_get_song_art(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT cover FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return {stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Album db_get_album_info(AlbumID album_id)
{
    Album album{};
    sqlite3_stmt* stmt;
    const char* query = "SELECT name, length, year FROM Albums WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    album.id = album_id;
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (name != nullptr)
        album.name = name;
    album.length = sqlite3_column_double(stmt, 1);
    album.year = sqlite3_column_int(stmt, 2);
    sqlite3_finalize(stmt);
    return album;
}

void db_free_art(Art* art)
{
    sqlite3_finalize(static_cast<sqlite3_stmt*>(art->sqlite_stmt));
}

Art db_get_album_art(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT cover FROM Albums WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
        return Art{stmt, NULL, 0};
    const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0));
    int blob_bytes = sqlite3_column_bytes(stmt, 0);
    return Art{stmt, blob, static_cast<size_t>(blob_bytes)};
}

Artist db_get_artist_info(ArtistID artist_id)
{
    Artist artist{};
    artist.id = artist_id;
    
    sqlite3_stmt* stmt;
    const char* query = "SELECT name FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
    {
        SPDLOG_INFO("{}", artist_id);
    }
    assert(res == SQLITE_ROW);
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (name != nullptr)
        artist.name = name;

    return artist;
}

Playlist db_get_playlist_info(PlaylistID playlist_id)
{
    Playlist playlist{};
    playlist.id = playlist_id;

    sqlite3_stmt* stmt;
    const char* query = "SELECT name, length FROM Playlists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    playlist.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    playlist.length = sqlite3_column_double(stmt, 1);
    sqlite3_finalize(stmt);
    return playlist;
}

AlbumID db_get_album_from_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM AlbumSong WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    AlbumID album_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return album_id;
}

ArtistID db_get_artist_from_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistSong WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

ArtistID db_get_artist_from_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistAlbum WHERE album_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

std::vector<SongTrackID> db_get_songs_from_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id, track FROM AlbumSong WHERE album_id=?1 ORDER BY track";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);

    std::vector<SongTrackID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SongID song_id = sqlite3_column_int(stmt, 0);
        int track = sqlite3_column_int(stmt, 1);
        songs.emplace_back(song_id, track);
    }

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<SongTrackID> db_get_songs_from_playlist(PlaylistID playlist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id, track FROM PlaylistSong WHERE playlist_id=?1 ORDER BY track";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);

    std::vector<SongTrackID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SongID song_id = sqlite3_column_int(stmt, 0);
        int track = sqlite3_column_int(stmt, 1);
        songs.emplace_back(song_id, track);
    }

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<SongID> db_get_songs_from_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT song_id FROM ArtistSong WHERE artist_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);

    std::vector<SongID> songs{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        songs.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return songs;
}

std::vector<AlbumID> db_get_albums_from_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT album_id FROM ArtistAlbum WHERE artist_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);

    std::vector<AlbumID> albums{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        albums.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return albums;
}

std::vector<PlaylistID> db_get_playlists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Playlists";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 

    std::vector<PlaylistID> playlists{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
        playlists.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return playlists;
}

ArtistID db_get_artist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Artists WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return artist_id;
}

void db_create_artist(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Artists (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

ArtistID db_create_default_artist()
{
    const char* query = "INSERT INTO Artists DEFAULT VALUES";
    sqlite3_exec(ctx.db, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(ctx.db);
}

PlaylistID db_create_default_playlist()
{
    const char* query = "INSERT INTO Playlists DEFAULT VALUES";
    sqlite3_exec(ctx.db, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(ctx.db);
}

AlbumID db_create_default_album()
{
    const char* query = "INSERT INTO Albums DEFAULT VALUES";
    sqlite3_exec(ctx.db, query, nullptr, nullptr, nullptr);
    return sqlite3_last_insert_rowid(ctx.db);
}

void db_create_song(const char* title, const char* song_path, unsigned int year, double song_length, const uint8_t* art, int art_size)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Songs (title, path, year, length, cover) VALUES (?1, ?2, ?3, ?4, ?5);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, song_path, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, year);
    sqlite3_bind_double(stmt, 4, song_length);
    sqlite3_bind_blob(stmt, 5, art, art_size, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (res == SQLITE_CONSTRAINT)
        SPDLOG_WARN("song title={} path={} exists in db already", title, song_path);
}

SearchResultID<SongID> db_search_songs(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    query = "SELECT COUNT(id) FROM Songs WHERE title LIKE ?1"; 
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Songs WHERE title LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return search_result_id;
}

SearchResultID<AlbumID> db_search_albums(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    query = "SELECT COUNT(id) FROM Albums WHERE title LIKE ?1"; 
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Albums WHERE title LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return search_result_id;
}

SearchResultID<ArtistID> db_search_artists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<ArtistID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    query = "SELECT COUNT(id) FROM Artists WHERE title LIKE ?1"; 
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Artists WHERE title LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return search_result_id;
}

SearchResultID<PlaylistID> db_search_playlists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<PlaylistID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    query = "SELECT COUNT(id) FROM Playlists WHERE title LIKE ?1"; 
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Playlists WHERE title LIKE ?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return search_result_id;
}

SearchResultID<SongID> db_search_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id{};
    sqlite3_stmt* stmt;
    const char* query;

    search_result_id.page_num = page_num;
    search_result_id.results_per_page = results_per_page;

    query = "SELECT COUNT(song_id) FROM ArtistSong WHERE artist_id=?1"; 
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    search_result_id.num_results = sqlite3_column_int(stmt, 0);
    search_result_id.num_pages = (search_result_id.num_results - 1) / search_result_id.results_per_page + 1;
    sqlite3_finalize(stmt);

    query = "SELECT song_id FROM ArtistSong WHERE artist_id=?1 LIMIT ?2 OFFSET ?3";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, results_per_page);
    sqlite3_bind_int(stmt, 3, results_per_page * (page_num - 1));
    while (sqlite3_step(stmt) == SQLITE_ROW)
        search_result_id.entries.push_back(sqlite3_column_int(stmt, 0));

    sqlite3_finalize(stmt);
    return search_result_id;
}

void db_update_song(SongID song_id, const char* new_title)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET title=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_title, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_album(AlbumID album_id, const char* new_name)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Albums SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_artist(ArtistID artist_id, const char* new_name)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Artists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_delete_artist(ArtistID artist_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Artists WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_create_artist_song(ArtistID artist_id, int song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistSong (artist_id, song_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_song_artist(SongID song_id, const char* artist)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT artist_id FROM ArtistSong WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    int res = sqlite3_step(stmt);
    ArtistID artist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);

    if (artist_id != INVALID_ID)
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
    if (artist_id == INVALID_ID)
    {
        db_create_artist(artist);
        artist_id = db_get_artist_id(artist);
    }
    db_create_artist_song(artist_id, song_id);
}

void db_delete_album(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Albums WHERE album_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_delete_song(SongID song_id)
{
    sqlite3_stmt* stmt;
    const char* query = "DELETE FROM Songs WHERE song_id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

AlbumID db_get_album_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Albums WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    AlbumID album_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return album_id;
}

void db_create_album(const char* name, unsigned int year, const uint8_t* art, int art_size)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Albums (name, year, cover) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, year);
    sqlite3_bind_blob(stmt, 3, art, art_size, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_create_album_song(AlbumID album_id, SongID song_id, int track)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, track);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    db_update_album_length(album_id);
}

PlaylistID db_get_playlist_id(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Playlists WHERE name=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    int res = sqlite3_step(stmt);
    PlaylistID playlist_id = (res == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : INVALID_ID;
    sqlite3_finalize(stmt);
    return playlist_id;
}

void db_create_playlist(const char* name)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Playlists (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_playlist(PlaylistID playlist_id, const char* new_name)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Playlists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_album_length(AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Albums SET length=(SELECT SUM(length) FROM Songs S JOIN AlbumSong A ON S.id=A.song_id WHERE A.album_id=?1) WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_song_lyrics(SongID song_id, const char* lyrics)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET lyrics=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, lyrics, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_update_song_volume(SongID song_id, float volume)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Songs SET volume=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_double(stmt, 1, volume);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void db_create_playlist_song(PlaylistID playlist_id, SongID song_id, int track)
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

bool db_exists_artist_album(ArtistID artist_id, AlbumID album_id)
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

int db_get_random_song()
{
    SongID song_id = INVALID_ID;
    sqlite3_stmt* stmt;
    const char* query = "SELECT id FROM Songs ORDER BY random() LIMIT 1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
        song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return song_id;
}

void db_get_song_lyrics(SongID song_id, char* lyrics_buffer, size_t lyrics_buffer_size)
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT lyrics FROM Songs WHERE id=?1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, song_id);
    [[maybe_unused]] int res = sqlite3_step(stmt);
    assert(res == SQLITE_ROW);
    const char* lyrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    snprintf(lyrics_buffer, lyrics_buffer_size, "%s", (lyrics) ? lyrics : "");
    sqlite3_finalize(stmt);
}

int db_get_num_songs()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Songs";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_songs = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_songs;
}

int db_get_num_albums()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Albums";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_albums = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_albums;
}

int db_get_num_artists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Artists";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_artists = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_artists;
}

int db_get_num_playlists()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(id) FROM Playlists";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_playlists = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_playlists;
}

int db_get_num_pins()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(order) FROM Pins";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_step(stmt);
    int num_pins = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return num_pins;
}

std::vector<PinID> db_get_pins()
{
    sqlite3_stmt* stmt;
    const char* query = "SELECT id, mp_type, pin_order FROM Pins ORDER BY pin_order";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    std::vector<PinID> pin_ids{};
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        MPUIDEnum type = static_cast<MPUIDEnum>(sqlite3_column_int(stmt, 1));
        pin_ids.emplace_back(db_get_uid(id, type), sqlite3_column_int(stmt, 2));
    }
    sqlite3_finalize(stmt);
    return pin_ids;
}

void db_create_artist_album(ArtistID artist_id, AlbumID album_id)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, artist_id);
    sqlite3_bind_int(stmt, 2, album_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool db_create_pin(int id, MPUIDEnum type)
{
    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Pins (id, mp_type, pin_order) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_int(stmt, 2, type);
    sqlite3_bind_int(stmt, 3, db_get_num_pins() + 1);
    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return res == SQLITE_DONE;
}

