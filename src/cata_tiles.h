#pragma once
#ifndef CATA_TILES_H
#define CATA_TILES_H

#include <cstddef>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "sdl_wrappers.h"
#include "animation.h"
#include "lightmap.h"
#include "line.h"
#include "options.h"
#include "game_constants.h"
#include "weather.h"
#include "enums.h"
#include "weighted_list.h"

class Creature;
class player;
class JsonObject;

extern void set_displaybuffer_rendertarget();

/** Structures */
struct tile_type {
    // fg and bg are both a weighted list of lists of sprite IDs
    weighted_int_list<std::vector<int>> fg, bg;
    bool multitile = false;
    bool rotates = false;
    int height_3d = 0;
    point offset = point_zero;

    std::vector<std::string> available_subtiles;
};

/* Enums */
enum MULTITILE_TYPE {
    center,
    corner,
    edge,
    t_connection,
    end_piece,
    unconnected,
    open_,
    broken,
    num_multitile_types
};
// Make sure to change TILE_CATEGORY_IDS if this changes!
enum TILE_CATEGORY {
    C_NONE,
    C_VEHICLE_PART,
    C_TERRAIN,
    C_ITEM,
    C_FURNITURE,
    C_TRAP,
    C_FIELD,
    C_LIGHTING,
    C_MONSTER,
    C_BULLET,
    C_HIT_ENTITY,
    C_WEATHER,
};

class texture
{
    private:
        std::shared_ptr<SDL_Texture> sdl_texture_ptr;
        SDL_Rect srcrect = { 0, 0, 0, 0 };

    public:
        texture( std::shared_ptr<SDL_Texture> ptr, const SDL_Rect rect ) : sdl_texture_ptr( ptr ),
            srcrect( rect ) { }
        texture() = default;

        /// Returns the width (first) and height (second) of the stored texture.
        std::pair<int, int> dimension() const {
            return std::make_pair( srcrect.w, srcrect.h );
        }
        /// Interface to @ref SDL_RenderCopyEx, using this as the texture, and
        /// null as source rectangle (render the whole texture). Other parameters
        /// are simply passed through.
        int render_copy_ex( const SDL_Renderer_Ptr &renderer, const SDL_Rect *const dstrect,
                            const double angle,
                            const SDL_Point *const center, const SDL_RendererFlip flip ) const {
            return SDL_RenderCopyEx( renderer.get(), sdl_texture_ptr.get(), &srcrect, dstrect, angle, center,
                                     flip );
        }
};

extern SDL_Texture_Ptr alt_rect_tex;
extern bool alt_rect_tex_enabled;
extern void draw_alt_rect( const SDL_Renderer_Ptr &renderer, const SDL_Rect &rect,
                           Uint32 r, Uint32 g, Uint32 b );

// a texture pool to avoid recreating textures every time player changes their view
// at most 142 out of 144 textures can be in use due to regular player movement
//  (moving from submap corner to new corner) with MAPSIZE = 11
// textures are dumped when the player moves more than one submap in one update
//  (teleporting, z-level change) to prevent running out of the remaining pool
struct minimap_shared_texture_pool {
    std::vector<SDL_Texture_Ptr> texture_pool;
    std::set<int> active_index;
    std::vector<int> inactive_index;
    minimap_shared_texture_pool() {
        reinit();
    }

    void reinit() {
        inactive_index.clear();
        texture_pool.resize( ( MAPSIZE + 1 ) * ( MAPSIZE + 1 ) );
        for( int i = 0; i < static_cast<int>( texture_pool.size() ); i++ ) {
            inactive_index.push_back( i );
        }
    }

    //reserves a texture from the inactive group and returns tracking info
    SDL_Texture_Ptr request_tex( int &i ) {
        if( inactive_index.empty() ) {
            //shouldn't be happening, but minimap will just be default color instead of crashing
            return nullptr;
        }
        const int index = inactive_index.back();
        inactive_index.pop_back();
        active_index.insert( index );
        i = index;
        return std::move( texture_pool[index] );
    }

    //releases the provided texture back into the inactive pool to be used again
    //called automatically in the submap cache destructor
    void release_tex( int i, SDL_Texture_Ptr ptr ) {
        const auto it = active_index.find( i );
        if( it == active_index.end() ) {
            return;
        }
        inactive_index.push_back( i );
        active_index.erase( i );
        texture_pool[i] = std::move( ptr );
    }
};

struct minimap_submap_cache {
    //the color stored for each submap tile
    std::vector< SDL_Color > minimap_colors;
    //checks if the submap has been looked at by the minimap routine
    bool touched;
    //the texture updates are drawn to
    SDL_Texture_Ptr minimap_tex;
    //the submap being handled
    int texture_index;
    //the list of updates to apply to the texture
    //reduces render target switching to once per submap
    std::vector<point> update_list;
    //if the submap has been drawn to screen during the current draw cycle
    bool drawn;
    //flag used to indicate that the texture needs to be cleared before first use
    bool ready;
    minimap_shared_texture_pool &pool;

    //reserve the SEEX * SEEY submap tiles
    minimap_submap_cache( minimap_shared_texture_pool &pool );
    minimap_submap_cache( minimap_submap_cache && );
    //handle the release of the borrowed texture
    ~minimap_submap_cache();
};

class tileset
{
    private:
        std::string tileset_id;

        int tile_width;
        int tile_height;

        // multiplier for pixel-doubling tilesets
        float tile_pixelscale;

        std::vector<texture> tile_values;
        std::vector<texture> shadow_tile_values;
        std::vector<texture> night_tile_values;
        std::vector<texture> overexposed_tile_values;
        std::vector<texture> memory_tile_values;

        std::unordered_map<std::string, tile_type> tile_ids;

        static const texture *get_if_available( const size_t index,
                                                const decltype( shadow_tile_values ) &tiles ) {
            return index < tiles.size() ? &( tiles[index] ) : nullptr;
        }

        friend class tileset_loader;

    public:
        int get_tile_width() const {
            return tile_width;
        }
        int get_tile_height() const {
            return tile_height;
        }
        float get_tile_pixelscale() const {
            return tile_pixelscale;
        }
        const std::string &get_tileset_id() const {
            return tileset_id;
        }

        const texture *get_tile( const size_t index ) const {
            return get_if_available( index, tile_values );
        }
        const texture *get_night_tile( const size_t index ) const {
            return get_if_available( index, night_tile_values );
        }
        const texture *get_shadow_tile( const size_t index ) const {
            return get_if_available( index, shadow_tile_values );
        }
        const texture *get_overexposed_tile( const size_t index ) const {
            return get_if_available( index, overexposed_tile_values );
        }
        const texture *get_memory_tile( const size_t index ) const {
            return get_if_available( index, memory_tile_values );
        }

        tile_type &create_tile_type( const std::string &id, tile_type &&new_tile_type );
        const tile_type *find_tile_type( const std::string &id ) const;
};

class tileset_loader
{
    private:
        tileset &ts;
        const SDL_Renderer_Ptr &renderer;

        int sprite_offset_x;
        int sprite_offset_y;

        int sprite_width;
        int sprite_height;

        int offset = 0;
        int sprite_id_offset = 0;
        int size = 0;

        struct {
            int R;
            int G;
            int B;
        };

        int tile_atlas_width;

        void ensure_default_item_highlight();

        void copy_surface_to_texture( const SDL_Surface_Ptr &surf, const point &offset,
                                      std::vector<texture> &target );
        void create_textures_from_tile_atlas( const SDL_Surface_Ptr &tile_atlas, const point &offset );

        void process_variations_after_loading( weighted_int_list<std::vector<int>> &v );

        void add_ascii_subtile( tile_type &curr_tile, const std::string &t_id, int fg,
                                const std::string &s_id );
        void load_ascii_set( JsonObject &entry );
        /**
         * Create a new tile_type, add it to tile_ids (using <B>id</B>).
         * Set the fg and bg properties of it (loaded from the json object).
         * Makes sure each is either -1, or in the interval [0,size).
         * If it's in that interval, adds offset to it, if it's not in the
         * interval (and not -1), throw an std::string error.
         */
        tile_type &load_tile( JsonObject &entry, const std::string &id );

        void load_tile_spritelists( JsonObject &entry, weighted_int_list<std::vector<int>> &vs,
                                    const std::string &objname );

        void load_ascii( JsonObject &config );
        /** Load tileset, R,G,B, are the color components of the transparent color
         * Returns the number of tiles that have been loaded from this tileset image
         * @throw std::exception If the image can not be loaded.
         */
        void load_tileset( const std::string &path );
        /**
         * Load tiles from json data.This expects a "tiles" array in
         * <B>config</B>. That array should contain all the tile definition that
         * should be taken from an tileset image.
         * Because the function only loads tile definitions for a single tileset
         * image, only tile indices (tile_type::fg tile_type::bg) in the interval
         * [0,size].
         * The <B>offset</B> is automatically added to the tile index.
         * sprite offset dictates where each sprite should render in its tile
         * @throw std::exception On any error.
         */
        void load_tilejson_from_file( JsonObject &config );
        /**
         * Helper function called by load.
         * @throw std::exception On any error.
         */
        void load_internal( JsonObject &config, const std::string &tileset_root,
                            const std::string &img_path );
    public:
        tileset_loader( tileset &ts, const SDL_Renderer_Ptr &r ) : ts( ts ), renderer( r ) {
        }
        /**
         * @throw std::exception On any error.
         * @param tileset_id Ident of the tileset, as it appears in the options.
         * @param precheck If tue, only loads the meta data of the tileset (tile dimensions).
         */
        void load( const std::string &tileset_id, bool precheck );
};

enum text_alignment {
    TEXT_ALIGNMENT_LEFT,
    TEXT_ALIGNMENT_CENTER,
    TEXT_ALIGNMENT_RIGHT,
};

struct formatted_text {
    std::string text;
    int color;
    text_alignment alignment;

    formatted_text( const std::string &text, const int color, const text_alignment alignment )
        : text( text ), color( color ), alignment( alignment ) {
    }

    formatted_text( const std::string &text, const int color, const direction direction );
};

class cata_tiles
{
    public:
        cata_tiles( const SDL_Renderer_Ptr &render );
        ~cata_tiles();
    public:
        /** Reload tileset, with the given scale. Scale is divided by 16 to allow for scales < 1 without risking
         *  float inaccuracies. */
        void set_draw_scale( int scale );

    public:
        /** Draw to screen */
        void draw( int destx, int desty, const tripoint &center, int width, int height,
                   std::multimap<point, formatted_text> &overlay_strings );

        /** Minimap functionality */
        void draw_minimap( int destx, int desty, const tripoint &center, int width, int height );
        void draw_rhombus( int destx, int desty, int size, SDL_Color color, int widthLimit,
                           int heightLimit );
    protected:
        /** How many rows and columns of tiles fit into given dimensions **/
        void get_window_tile_counts( const int width, const int height, int &columns, int &rows ) const;

        const tile_type *find_tile_with_season( std::string &id );
        const tile_type *find_tile_looks_like( std::string &id, TILE_CATEGORY category );
        bool find_overlay_looks_like( const bool male, const std::string &overlay, std::string &draw_id );

        bool draw_from_id_string( std::string id, const tripoint &pos, int subtile, int rota, lit_level ll,
                                  bool apply_night_vision_goggles );
        bool draw_from_id_string( std::string id, TILE_CATEGORY category,
                                  const std::string &subcategory, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles );
        bool draw_from_id_string( std::string id, const tripoint &pos, int subtile, int rota, lit_level ll,
                                  bool apply_night_vision_goggles, int &height_3d );
        bool draw_from_id_string( std::string id, TILE_CATEGORY category,
                                  const std::string &subcategory, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles, int &height_3d );
        bool draw_sprite_at( const tile_type &tile, const weighted_int_list<std::vector<int>> &svlist,
                             int x, int y, unsigned int loc_rand, bool rota_fg, int rota, lit_level ll,
                             bool apply_night_vision_goggles );
        bool draw_sprite_at( const tile_type &tile, const weighted_int_list<std::vector<int>> &svlist,
                             int x, int y, unsigned int loc_rand, bool rota_fg, int rota, lit_level ll,
                             bool apply_night_vision_goggles, int &height_3d );
        bool draw_tile_at( const tile_type &tile, int x, int y, unsigned int loc_rand, int rota,
                           lit_level ll, bool apply_night_vision_goggles, int &height_3d );

        ///@throws std::exception upon errors.
        ///@returns Always a valid pointer.
        SDL_Surface_Ptr create_tile_surface();

        /* Tile Picking */
        void get_tile_values( const int t, const int *tn, int &subtile, int &rotation );
        void get_connect_values( const tripoint &p, int &subtile, int &rotation, int connect_group );
        void get_terrain_orientation( const tripoint &p, int &rota, int &subtype );
        void get_rotation_and_subtile( const char val, int &rota, int &subtype );

        /** Drawing Layers */
        bool apply_vision_effects( const tripoint &pos, const visibility_type visibility );
        bool draw_terrain( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_terrain_from_memory( const tripoint &p, int &height_3d );
        bool draw_terrain_below( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_furniture( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_graffiti( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_trap( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_field_or_item( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_vpart( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_vpart_below( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_critter_at( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_critter_at_below( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_zone_mark( const tripoint &p, lit_level ll, int &height_3d );
        bool draw_entity( const Creature &critter, const tripoint &p, lit_level ll, int &height_3d );
        void draw_entity_with_overlays( const player &pl, const tripoint &p, lit_level ll, int &height_3d );

        bool draw_item_highlight( const tripoint &pos );

    public:
        // Animation layers
        void init_explosion( const tripoint &p, int radius );
        void draw_explosion_frame();
        void void_explosion();

        void init_custom_explosion_layer( const std::map<tripoint, explosion_tile> &layer );
        void draw_custom_explosion_frame();
        void void_custom_explosion();

        void init_draw_bullet( const tripoint &p, std::string name );
        void draw_bullet_frame();
        void void_bullet();

        void init_draw_hit( const tripoint &p, std::string name );
        void draw_hit_frame();
        void void_hit();

        void draw_footsteps_frame();

        // pseudo-animated layer, not really though.
        void init_draw_line( const tripoint &p, std::vector<tripoint> trajectory,
                             std::string line_end_name, bool target_line );
        void draw_line();
        void void_line();

        void init_draw_cursor( const tripoint &p );
        void draw_cursor();
        void void_cursor();

        void init_draw_highlight( const tripoint &p );
        void draw_highlight();
        void void_highlight();

        void init_draw_weather( weather_printable weather, std::string name );
        void draw_weather_frame();
        void void_weather();

        void init_draw_sct();
        void draw_sct_frame( std::multimap<point, formatted_text> &overlay_strings );
        void void_sct();

        void init_draw_zones( const tripoint &start, const tripoint &end, const tripoint &offset );
        void draw_zones_frame();
        void void_zones();

    public:
        /**
         * Initialize the current tileset (load tile images, load mapping), using the current
         * tileset as it is set in the options.
         * @param tileset_id Ident of the tileset, as it appears in the options.
         * @param precheck If true, only loads the meta data of the tileset (tile dimensions).
         * @param force If true, forces loading the tileset even if it is already loaded.
         * @throw std::exception On any error.
         */
        void load_tileset( const std::string &tileset_id, bool precheck = false, bool force = false );
        /**
         * Reinitializes the current tileset, like @ref init, but using the original screen information.
         * @throw std::exception On any error.
         */
        void reinit();

        void reinit_minimap();

        int get_tile_height() const {
            return tile_height;
        }
        int get_tile_width() const {
            return tile_width;
        }
        float get_tile_ratiox() const {
            return tile_ratiox;
        }
        float get_tile_ratioy() const {
            return tile_ratioy;
        }
        void do_tile_loading_report();
        point player_to_screen( int x, int y ) const;
        static std::vector<options_manager::id_and_option> build_renderer_list();
    protected:
        template <typename maptype>
        void tile_loading_report( const maptype &tiletypemap, const std::string &label,
                                  const std::string &prefix = "" );
        template <typename arraytype>
        void tile_loading_report( const arraytype &array, int array_length, const std::string &label,
                                  const std::string &prefix = "" );
        template <typename basetype>
        void tile_loading_report( size_t count, const std::string &label, const std::string &prefix );
        /**
         * Generic tile_loading_report, begin and end are iterators, id_func translates the iterator
         * to an id string (result of id_func must be convertible to string).
         */
        template<typename Iter, typename Func>
        void lr_generic( Iter begin, Iter end, Func id_func, const std::string &label,
                         const std::string &prefix );
        /** Lighting */
        void init_light();

        /** Variables */
        const SDL_Renderer_Ptr &renderer;
        std::unique_ptr<tileset> tileset_ptr;

        int tile_height = 0;
        int tile_width = 0;
        // The width and height of the area we can draw in,
        // measured in map coordinates, *not* in pixels.
        int screentile_width = 0;
        int screentile_height = 0;
        float tile_ratiox = 0.0;
        float tile_ratioy = 0.0;

        bool in_animation;

        bool do_draw_explosion;
        bool do_draw_custom_explosion;
        bool do_draw_bullet;
        bool do_draw_hit;
        bool do_draw_line;
        bool do_draw_cursor;
        bool do_draw_highlight;
        bool do_draw_weather;
        bool do_draw_sct;
        bool do_draw_zones;

        tripoint exp_pos;
        int exp_rad;

        std::map<tripoint, explosion_tile> custom_explosion_layer;

        tripoint bul_pos;
        std::string bul_id;

        tripoint hit_pos;
        std::string hit_entity_id;

        tripoint line_pos;
        bool is_target_line;
        std::vector<tripoint> line_trajectory;
        std::string line_endpoint_id;

        std::vector<tripoint> cursors;
        std::vector<tripoint> highlights;

        weather_printable anim_weather;
        std::string weather_name;

        tripoint zone_start;
        tripoint zone_end;
        tripoint zone_offset;

        // offset values, in tile coordinates, not pixels
        int o_x = 0;
        int o_y = 0;
        // offset for drawing, in pixels.
        int op_x = 0;
        int op_y = 0;

    private:
        int last_pos_x = 0;
        int last_pos_y = 0;
        /**
         * Tracks active night vision goggle status for each draw call.
         * Allows usage of night vision tilesets during sprite rendering.
         */
        bool nv_goggles_activated;

        //pixel minimap cache methods
        SDL_Texture_Ptr create_minimap_cache_texture( int tile_width, int tile_height );
        void process_minimap_cache_updates();
        void update_minimap_cache( const tripoint &loc, const SDL_Color &color );
        void prepare_minimap_cache_for_updates();
        void clear_unused_minimap_cache();

        //the minimap texture pool which is used to reduce new texture allocation spam
        minimap_shared_texture_pool tex_pool;
        std::map<tripoint, minimap_submap_cache> minimap_cache;

        //persistent tiled minimap values
        void init_minimap( int destx, int desty, int width, int height );
        bool minimap_prep;
        point minimap_min;
        point minimap_max;
        point minimap_tiles_range;
        point minimap_tile_size;
        point minimap_tiles_limit;
        int minimap_drawn_width;
        int minimap_drawn_height;
        int minimap_border_width;
        int minimap_border_height;
        SDL_Rect minimap_clip_rect;
        //track the previous viewing area to determine if the minimap cache needs to be cleared
        tripoint previous_submap_view;
        bool minimap_reinit_flag; //set to true to force a reallocation of minimap details
        //place all submaps on this texture before rendering to screen
        //replaces clipping rectangle usage while SDL still has a flipped y-coordinate bug
        SDL_Texture_Ptr main_minimap_tex;
        // SDL_RenderFillRect replacement handler
        void handle_draw_rect( const SDL_Renderer_Ptr &renderer, const SDL_Rect &rect,
                               Uint32 r, Uint32 g, Uint32 b );
};

#endif
