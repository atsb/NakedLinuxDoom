set(GAME_SOURCE_FILES
am_map.c	i_main.c	p_ceilng.c	p_setup.c	r_sky.c
i_net.c		p_doors.c	p_sight.c	r_things.c
d_items.c	i_sound.c	p_enemy.c	p_spec.c	s_sound.c
d_main.c	i_system.c	p_floor.c	p_switch.c	sounds.c
d_net.c		i_video.c	p_telept.c	st_lib.c
doomdef.c	info.c		p_inter.c	p_tick.c	st_stuff.c
doomstat.c	m_argv.c	p_lights.c	p_user.c	tables.c
dstrings.c	m_bbox.c	p_map.c		r_bsp.c		v_video.c
f_finale.c	m_cheat.c	p_maputl.c	r_data.c
f_wipe.c	m_menu.c	p_mobj.c	r_draw.c	w_wad.c
g_game.c	m_misc.c	p_plats.c	r_main.c	wi_stuff.c
hu_lib.c	m_random.c	p_pspr.c	r_plane.c	z_zone.c
hu_stuff.c	p_saveg.c	r_segs.c	mmus2mid.c)

set(GAME_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")

set(SOURCE_FILES ${COMMON_SOURCE_FILES} ${GAME_SOURCE_FILES})

set(EXTRA_LIBS m SDL2::SDL2main SDL2::SDL2 SDL2::net SDL2::mixer)

add_executable("doom" ${GAME_SOURCE_FILES})

target_include_directories("doom" PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries("doom" ${EXTRA_LIBS})

if(MSVC)
    set_target_properties("doom" PROPERTIES
                          LINK_FLAGS "/MANIFEST:NO")
endif()
