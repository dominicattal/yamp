#ifndef MP_H
#define MP_H

#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <functional>
#include <memory>

enum LoopMode {
    LOOP_NONE,
    LOOP_GROUP,
    LOOP_TRACK
};

struct Song {
    std::string title;
    std::string path;
    int id;
};

struct Artist {
    std::string name;
    int id;
};

struct Album {
    std::string name;
    int id;
};

struct Playlist {
    std::string name;
    int id;
};

struct AlbumSong {
    int album_id;
    int song_id;
    int track;
};

struct PlaylistSong {
    int playlist_id;
    int song_id;
    int track;
};

struct ArtistSong {
    int artist_id;
    int song_id;
};

struct ArtistAlbum {
    int artist_id;
    int album_id;
};

struct SongTrack {
    int song_id;
    int track;
};

struct FrontCover {
    unsigned char* data;
    int width;
    int height;
};

using SongCallback = std::function<void(Song*)>;

struct MPContext {
    const Song* current_song;
    std::vector<Song> songs;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    std::vector<Playlist> playlists;
    std::vector<AlbumSong> album_songs;
    std::vector<ArtistSong> artist_songs;
    std::vector<ArtistAlbum> artist_albums;
    std::vector<PlaylistSong> playlist_songs;

    SongCallback song_callback;

    // this stores songs the user explicity queues up
    std::deque<const Song*> queue;

    // this stores the order songs should be play in the group
    std::deque<SongTrack> group_queue;

    // whether mp is playing a group (playlist or album) or not.
    bool playing_group;
    // true if group is album, false otherwise
    bool group_is_album;
    // album_id if group_is_album == true, playlist_id otherwise
    int group_id;

    // whether to play songs in random order. when this is on, music will play even
    // if the end of the queue is reached
    bool shuffle;

    bool autoplay;

    int loop_mode;
};

extern MPContext mp_ctx;

// Initializes and cleans up music play context
void mp_init();
void mp_cleanup();

// Call this every frame. Checks whether current song ended or not
void mp_update();

// Add a song to the database. Right now, just add it to the database whether or not it exists already.
// Eventually, flow should be like preview song in ui -> add config -> choose to save to database
void mp_add_song(const std::string& song_path);
void mp_add_songs(const std::vector<std::string>& song_paths);
void mp_recursive_add_songs(const std::string& folder_path);

// Add a playlist to the database. Returns the id of the created playlist.
int mp_create_playlist();
void mp_rename_playlist_id(int playlist_id, const char* new_playlist_name);

// Add a song to a playlist.
void mp_add_song_id_to_playlist_id(int song_id, int playlist_id);

// Immediately play a song
void mp_play_song(int song_id);

// Queue a song or mulitple songs
void mp_queue_song(int song_id);

// Will pause if song is playing and resume if song is not playing
void mp_pause_or_resume();

void mp_toggle_shuffle();
void mp_toggle_autoplay();

void mp_play_playlist(int playlist_id);
void mp_play_album(int album_id);

// Load song cover art from memory
FrontCover mp_song_front_cover_load(int song_id);
void mp_song_front_cover_free(FrontCover* data);

// Search for songs based on query
std::vector<int> mp_search_songs(const char* search_query);

const Song* mp_get_song_from_id(int id);
const Album* mp_get_album_from_id(int id);
const Artist* mp_get_artist_from_id(int id);
const Playlist* mp_get_playlist_from_id(int id);

int mp_get_album_id_from_song_id(int song_id);
int mp_get_artist_id_from_song_id(int song_id);
int mp_get_artist_id_from_album_id(int album_id);
std::vector<SongTrack> mp_get_song_ids_from_album_id(int album_id);
int mp_get_num_tracks_in_playlist_id(int playlist_id);
std::vector<SongTrack> mp_get_song_ids_from_playlist_id(int playlist_id);
std::vector<int> mp_get_album_ids_from_artist_id(int artist_id);
std::vector<int> mp_get_song_ids_from_artist_id(int artist_id);

// Skip current song in queue
void mp_queue_skip();
void mp_queue_clear();

#endif
