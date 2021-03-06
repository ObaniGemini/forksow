/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
==========================================================================

- SPLITMODELS -

==========================================================================
*/

// - Adding the Player model using Skeletal animation blending
// by Jalisk0

#include "client/client.h"
#include "cg_local.h"
#include "client/renderer/r_local.h"

pmodel_t cg_entPModels[MAX_EDICTS];
PlayerModelMetadata *cg_PModelInfos;

//======================================================================
//						PlayerModel Registering
//======================================================================

void CG_PModelsInit() {
	memset( cg_entPModels, 0, sizeof( cg_entPModels ) );
	cg_PModelInfos = NULL;
}

void CG_PModelsShutdown() {
	PlayerModelMetadata * next = cg_PModelInfos;
	while( next != NULL ) {
		PlayerModelMetadata * curr = next;
		next = next->next;
		CG_Free( curr );
	}
}

/*
* CG_ResetPModels
*/
void CG_ResetPModels( void ) {
	for( int i = 0; i < MAX_EDICTS; i++ ) {
		cg_entPModels[i].flash_time = cg_entPModels[i].barrel_time = 0;
		memset( &cg_entPModels[i].animState, 0, sizeof( pmodel_animationstate_t ) );
	}
	memset( &cg.weapon, 0, sizeof( cg.weapon ) );
}

static Mat4 EulerAnglesToMat4( float pitch, float yaw, float roll ) {
	mat3_t axis;
	AnglesToAxis( tv( pitch, yaw, roll ), axis );

	Mat4 m = Mat4::Identity();

	m.col0.x = axis[ 0 ];
	m.col0.y = axis[ 1 ];
	m.col0.z = axis[ 2 ];
	m.col1.x = axis[ 3 ];
	m.col1.y = axis[ 4 ];
	m.col1.z = axis[ 5 ];
	m.col2.x = axis[ 6 ];
	m.col2.y = axis[ 7 ];
	m.col2.z = axis[ 8 ];

	return m;
}

static Mat4 Mat4_Translation( float x, float y, float z ) {
	return Mat4(
		1, 0, 0, x,
		0, 1, 0, y,
		0, 0, 1, z,
		0, 0, 0, 1
	);
}

/*
* CG_ParseAnimationScript
*
* Reads the animation config file.
*
* 0 = first frame
* 1 = lastframe
* 2 = looping frames
* 3 = fps
*
* Note: The animations count begins at 1, not 0. I preserve zero for "no animation change"
*/
static bool CG_ParseAnimationScript( PlayerModelMetadata *metadata, char *filename ) {
	int num_clips = 1;

	int filenum;
	int length = trap_FS_FOpenFile( filename, &filenum, FS_READ );
	if( length == -1 ) {
		CG_Printf( "Couldn't find animation script: %s\n", filename );
		return false;
	}

	uint8_t * buf = ( uint8_t * )CG_Malloc( length + 1 );
	length = trap_FS_Read( buf, length, filenum );
	trap_FS_FCloseFile( filenum );
	if( !length ) {
		CG_Free( buf );
		CG_Printf( "Couldn't load animation script: %s\n", filename );
		return false;
	}

	const char * ptr = ( const char * ) buf;
	while( ptr ) {
		const char * cmd = COM_ParseExt( &ptr, true );
		if( strcmp( cmd, "" ) == 0 )
			break;

		if( Q_stricmp( cmd, "upper_rotator_joints" ) == 0 ) {
			const char * joint_name = COM_ParseExt( &ptr, false );
			R_FindJointByName( metadata->model, joint_name, &metadata->upper_rotator_joints[ 0 ] );

			joint_name = COM_ParseExt( &ptr, false );
			R_FindJointByName( metadata->model, joint_name, &metadata->upper_rotator_joints[ 1 ] );
		}
		else if( Q_stricmp( cmd, "head_rotator_joint" ) == 0 ) {
			const char * joint_name  = COM_ParseExt( &ptr, false );
			R_FindJointByName( metadata->model, joint_name, &metadata->head_rotator_joint );
		}
		else if( Q_stricmp( cmd, "upper_root_joint" ) == 0 ) {
			const char * joint_name  = COM_ParseExt( &ptr, false );
			R_FindJointByName( metadata->model, joint_name, &metadata->upper_root_joint );
		}
		else if( Q_stricmp( cmd, "tag" ) == 0 ) {
			const char * joint_name = COM_ParseExt( &ptr, false );
			u8 joint_idx;
			if( R_FindJointByName( metadata->model, joint_name, &joint_idx ) ) {
				const char * tag_name = COM_ParseExt( &ptr, false );
				PlayerModelMetadata::Tag * tag = &metadata->tag_backpack;
				if( strcmp( tag_name, "tag_head" ) == 0 )
					tag = &metadata->tag_head;
				else if( strcmp( tag_name, "tag_weapon" ) == 0 )
					tag = &metadata->tag_weapon;

				float forward = atof( COM_ParseExt( &ptr, false ) );
				float right = atof( COM_ParseExt( &ptr, false ) );
				float up = atof( COM_ParseExt( &ptr, false ) );
				float pitch = atof( COM_ParseExt( &ptr, false ) );
				float yaw = atof( COM_ParseExt( &ptr, false ) );
				float roll = atof( COM_ParseExt( &ptr, false ) );

				tag->joint_idx = joint_idx;
				tag->transform = Mat4_Translation( forward, right, up ) * EulerAnglesToMat4( pitch, yaw, roll );
			}
			else {
				CG_Printf( "%s: Unknown joint name: %s\n", filename, joint_name );
				for( int i = 0; i < 7; i++ )
					COM_ParseExt( &ptr, false );
			}
		}
		else if( Q_stricmp( cmd, "clip" ) == 0 ) {
			if( num_clips == PMODEL_TOTAL_ANIMATIONS ) {
				CG_Printf( "%s: Too many animations\n", filename );
				break;
			}

			int start_frame = atoi( COM_ParseExt( &ptr, false ) );
			int end_frame = atoi( COM_ParseExt( &ptr, false ) );
			int loop_frames = atoi( COM_ParseExt( &ptr, false ) );
			int fps = atoi( COM_ParseExt( &ptr, false ) );
			fps = 30;

			PlayerModelMetadata::AnimationClip clip;
			clip.start_time = float( start_frame + 1 ) / float( fps );
			clip.duration = float( end_frame - start_frame ) / float( fps );
			clip.loop_from = clip.duration - float( loop_frames ) / float( fps );

			metadata->clips[ num_clips ] = clip;
			num_clips++;
		}
		else {
			CG_Printf( "%s: unrecognized cmd: %s\n", filename, cmd );
		}
	}

	CG_Free( buf );

	if( num_clips < PMODEL_TOTAL_ANIMATIONS ) {
		CG_Printf( "%s: Not enough animations (%i)\n", filename, num_clips );
		return false;
	}

	metadata->clips[ ANIM_NONE ].start_time = 0;
	metadata->clips[ ANIM_NONE ].duration = 0;
	metadata->clips[ ANIM_NONE ].loop_from = 0;

	return true;
}

/*
* CG_LoadPlayerModel
*/
static bool CG_LoadPlayerModel( PlayerModelMetadata *metadata, const char *filename ) {
	MICROPROFILE_SCOPEI( "Assets", "CG_LoadPlayerModel", 0xffffffff );

	bool loaded_model = false;
	char anim_filename[MAX_QPATH];
	char scratch[MAX_QPATH];

	Q_snprintfz( scratch, sizeof( scratch ), "%s.glb", filename );
	if( cgs.pure && !trap_FS_IsPureFile( scratch ) ) {
		return false;
	}

	metadata->model = CG_RegisterModel( scratch );

	// load animations script
	if( metadata->model ) {
		Q_snprintfz( anim_filename, sizeof( anim_filename ), "%s/animation.cfg", filename );
		if( !cgs.pure || trap_FS_IsPureFile( anim_filename ) ) {
			loaded_model = CG_ParseAnimationScript( metadata, anim_filename );
		}
	}

	// clean up if failed
	if( !loaded_model ) {
		metadata->model = NULL;
		return false;
	}

	metadata->name = CG_CopyString( filename );

	// load sexed sounds for this model
	CG_UpdateSexedSoundsRegistration( metadata );
	return true;
}

/*
* CG_RegisterPModel
* PModel is not exactly the model, but the indexes of the
* models contained in the pmodel and it's animation data
*/
PlayerModelMetadata *CG_RegisterPlayerModel( const char *filename ) {
	PlayerModelMetadata *metadata;

	for( metadata = cg_PModelInfos; metadata; metadata = metadata->next ) {
		if( !Q_stricmp( metadata->name, filename ) ) {
			return metadata;
		}
	}

	metadata = ( PlayerModelMetadata * )CG_Malloc( sizeof( PlayerModelMetadata ) );
	if( !CG_LoadPlayerModel( metadata, filename ) ) {
		CG_Free( metadata );
		return NULL;
	}

	metadata->next = cg_PModelInfos;
	cg_PModelInfos = metadata;

	return metadata;
}

/*
* CG_RegisterBasePModel
* Default fallback replacements
*/
void CG_RegisterBasePModel( void ) {
	char filename[MAX_QPATH];

	// metadata
	Q_snprintfz( filename, sizeof( filename ), "models/players/%s", DEFAULT_PLAYERMODEL );
	cgs.basePModelInfo = CG_RegisterPlayerModel( filename );

	Q_snprintfz( filename, sizeof( filename ), "models/players/%s/%s", DEFAULT_PLAYERMODEL, DEFAULT_PLAYERSKIN );
	cgs.baseSkin = trap_R_RegisterSkinFile( filename );
	if( !cgs.baseSkin ) {
		CG_Error( "'Default Player Model'(%s): Skin (%s) failed to load", DEFAULT_PLAYERMODEL, filename );
	}

	if( !cgs.basePModelInfo ) {
		CG_Error( "'Default Player Model'(%s): failed to load", DEFAULT_PLAYERMODEL );
	}
}

//======================================================================
//							tools
//======================================================================


/*
* CG_GrabTag
*/
bool CG_GrabTag( orientation_t *tag, entity_t *ent, const char *tagname ) {
	if( !ent->model ) {
		return false;
	}

	return trap_R_LerpTag( tag, ent->model, ent->frame, ent->oldframe, ent->backlerp, tagname );
}

/*
* CG_PlaceRotatedModelOnTag
*/
void CG_PlaceRotatedModelOnTag( entity_t *ent, entity_t *dest, orientation_t *tag ) {
	mat3_t tmpAxis;

	VectorCopy( dest->origin, ent->origin );

	for( int i = 0; i < 3; i++ )
		VectorMA( ent->origin, tag->origin[i] * ent->scale, &dest->axis[i * 3], ent->origin );

	VectorCopy( ent->origin, ent->origin2 );
	Matrix3_Multiply( ent->axis, tag->axis, tmpAxis );
	Matrix3_Multiply( tmpAxis, dest->axis, ent->axis );
}

/*
* CG_PlaceModelOnTag
*/
void CG_PlaceModelOnTag( entity_t *ent, entity_t *dest, const orientation_t *tag ) {
	VectorCopy( dest->origin, ent->origin );

	for( int i = 0; i < 3; i++ )
		VectorMA( ent->origin, tag->origin[i] * ent->scale, &dest->axis[i * 3], ent->origin );

	VectorCopy( ent->origin, ent->origin2 );
	Matrix3_Multiply( tag->axis, dest->axis, ent->axis );
}

/*
* CG_MoveToTag
* "move" tag must have an axis and origin set up. Use vec3_origin and axis_identity for "nothing"
*/
void CG_MoveToTag( vec3_t move_origin,
				   mat3_t move_axis,
				   const vec3_t space_origin,
				   const mat3_t space_axis,
				   const vec3_t tag_origin,
				   const mat3_t tag_axis ) {
	mat3_t tmpAxis;

	VectorCopy( space_origin, move_origin );

	for( int i = 0; i < 3; i++ )
		VectorMA( move_origin, tag_origin[i], &space_axis[i * 3], move_origin );

	Matrix3_Multiply( move_axis, tag_axis, tmpAxis );
	Matrix3_Multiply( tmpAxis, space_axis, move_axis );
}

/*
* CG_PModel_GetProjectionSource
* It asumes the player entity is up to date
*/
bool CG_PModel_GetProjectionSource( int entnum, orientation_t *tag_result ) {
	centity_t *cent;
	pmodel_t *pmodel;

	if( !tag_result ) {
		return false;
	}

	if( entnum < 1 || entnum >= MAX_EDICTS ) {
		return false;
	}

	cent = &cg_entities[entnum];
	if( cent->serverFrame != cg.frame.serverFrame ) {
		return false;
	}

	// see if it's the view weapon
	if( ISVIEWERENTITY( entnum ) && !cg.view.thirdperson ) {
		VectorCopy( cg.weapon.projectionSource.origin, tag_result->origin );
		Matrix3_Copy( cg.weapon.projectionSource.axis, tag_result->axis );
		return true;
	}

	// it's a 3rd person model
	pmodel = &cg_entPModels[entnum];
	VectorCopy( pmodel->projectionSource.origin, tag_result->origin );
	Matrix3_Copy( pmodel->projectionSource.axis, tag_result->axis );
	return true;
}

/*
* CG_AddRaceGhostShell
*/
static void CG_AddRaceGhostShell( entity_t *ent ) {
	entity_t shell;
	float alpha = cg_raceGhostsAlpha->value;

	clamp( alpha, 0, 1.0 );

	shell = *ent;
	shell.customSkin = NULL;

	if( shell.renderfx & RF_WEAPONMODEL ) {
		return;
	}

	shell.customShader = CG_MediaShader( cgs.media.shaderRaceGhostEffect );
	shell.renderfx |= ( RF_FULLBRIGHT | RF_NOSHADOW );
	shell.outlineHeight = 0;

	shell.color[0] *= alpha;
	shell.color[1] *= alpha;
	shell.color[2] *= alpha;
	shell.color[3] = 255 * alpha;

	CG_AddEntityToScene( &shell );
}

/*
* CG_AddShellEffects
*/
void CG_AddShellEffects( entity_t *ent, int effects ) {
	if( effects & EF_RACEGHOST ) {
		CG_AddRaceGhostShell( ent );
	}
}

/*
* CG_OutlineScaleForDist
*/
static float CG_OutlineScaleForDist( entity_t *e, float maxdist, float scale ) {
	float dist;
	vec3_t dir;

	if( e->renderfx & RF_WEAPONMODEL ) {
		return 0.14f;
	}

	// Kill if behind the view or if too far away
	VectorSubtract( e->origin, cg.view.origin, dir );
	dist = VectorNormalize2( dir, dir ) * cg.view.fracDistFOV;
	if( dist > maxdist ) {
		return 0;
	}

	if( !( e->renderfx & RF_WEAPONMODEL ) ) {
		if( DotProduct( dir, &cg.view.axis[AXIS_FORWARD] ) < 0 ) {
			return 0;
		}
	}

	dist *= scale;

	if( dist < 64 ) {
		return 0.14f;
	}
	if( dist < 128 ) {
		return 0.30f;
	}
	if( dist < 256 ) {
		return 0.42f;
	}
	if( dist < 512 ) {
		return 0.56f;
	}
	if( dist < 768 ) {
		return 0.70f;
	}

	return 1.0f;
}

/*
* CG_AddColoredOutLineEffect
*/
void CG_AddColoredOutLineEffect( entity_t *ent, int effects, uint8_t r, uint8_t g, uint8_t b, uint8_t a ) {
	if( !cg_outlineModels->integer || !( effects & EF_OUTLINE ) ) {
		ent->outlineHeight = 0;
		return;
	}

	ent->outlineHeight = CG_OutlineScaleForDist( ent, 4096, 1.0f );

	if( effects & EF_GODMODE ) {
		Vector4Set( ent->outlineColor, 255, 255, 255, a );
	} else {
		Vector4Set( ent->outlineColor, r, g, b, a );
	}
}


//======================================================================
//							animations
//======================================================================

// movement flags for animation control
#define ANIMMOVE_FRONT      ( 1 << 0 )  // Player is pressing fordward
#define ANIMMOVE_BACK       ( 1 << 1 )  // Player is pressing backpedal
#define ANIMMOVE_LEFT       ( 1 << 2 )  // Player is pressing sideleft
#define ANIMMOVE_RIGHT      ( 1 << 3 )  // Player is pressing sideright
#define ANIMMOVE_WALK       ( 1 << 4 )  // Player is pressing the walk key
#define ANIMMOVE_RUN        ( 1 << 5 )  // Player is running
#define ANIMMOVE_DUCK       ( 1 << 6 )  // Player is crouching
#define ANIMMOVE_SWIM       ( 1 << 7 )  // Player is swimming
#define ANIMMOVE_AIR        ( 1 << 8 )  // Player is at air, but not jumping
#define ANIMMOVE_DEAD       ( 1 << 9 )  // Player is a corpse

static int CG_MoveFlagsToUpperAnimation( uint32_t moveflags, int carried_weapon ) {
	if( moveflags & ANIMMOVE_DEAD )
		return ANIM_NONE;
	if( moveflags & ANIMMOVE_SWIM )
		return TORSO_SWIM;

	switch( carried_weapon ) {
		case WEAP_NONE:
			return TORSO_HOLD_BLADE; // fixme: a special animation should exist
		case WEAP_GUNBLADE:
			return TORSO_HOLD_BLADE;
		case WEAP_LASERGUN:
			return TORSO_HOLD_PISTOL;
		case WEAP_RIOTGUN:
		case WEAP_PLASMAGUN:
			return TORSO_HOLD_LIGHTWEAPON;
		case WEAP_ROCKETLAUNCHER:
		case WEAP_GRENADELAUNCHER:
			return TORSO_HOLD_HEAVYWEAPON;
		case WEAP_ELECTROBOLT:
			return TORSO_HOLD_AIMWEAPON;
	}

	return TORSO_HOLD_LIGHTWEAPON;
}

static int CG_MoveFlagsToLowerAnimation( uint32_t moveflags ) {
	if( moveflags & ANIMMOVE_DEAD )
		return ANIM_NONE;

	if( moveflags & ANIMMOVE_SWIM )
		return ( moveflags & ANIMMOVE_FRONT ) ? LEGS_SWIM_FORWARD : LEGS_SWIM_NEUTRAL;

	if( moveflags & ANIMMOVE_DUCK ) {
		if( moveflags & ( ANIMMOVE_WALK | ANIMMOVE_RUN ) )
			return LEGS_CROUCH_WALK;
		return LEGS_CROUCH_IDLE;
	}

	if( moveflags & ANIMMOVE_AIR )
		return LEGS_JUMP_NEUTRAL;

	if( moveflags & ANIMMOVE_RUN ) {
		// front/backward has priority over side movements
		if( moveflags & ANIMMOVE_FRONT )
			return LEGS_RUN_FORWARD;
		if( moveflags & ANIMMOVE_BACK )
			return LEGS_RUN_BACK;
		if( moveflags & ANIMMOVE_RIGHT )
			return LEGS_RUN_RIGHT;
		if( moveflags & ANIMMOVE_LEFT )
			return LEGS_RUN_LEFT;
		return LEGS_WALK_FORWARD;
	}

	if( moveflags & ANIMMOVE_WALK ) {
		// front/backward has priority over side movements
		if( moveflags & ANIMMOVE_FRONT )
			return LEGS_WALK_FORWARD;
		if( moveflags & ANIMMOVE_BACK )
			return LEGS_WALK_BACK;
		if( moveflags & ANIMMOVE_RIGHT )
			return LEGS_WALK_RIGHT;
		if( moveflags & ANIMMOVE_LEFT )
			return LEGS_WALK_LEFT;
		return LEGS_WALK_FORWARD;
	}

	return LEGS_STAND_IDLE;
}

static PlayerModelAnimationSet CG_GetBaseAnims( entity_state_t *state, const vec3_t velocity ) {
	constexpr float MOVEDIREPSILON = 0.3f;
	constexpr float WALKEPSILON = 5.0f;
	constexpr float RUNEPSILON = 220.0f;

	uint32_t moveflags = 0;
	vec3_t movedir;
	vec3_t hvel;
	mat3_t viewaxis;
	float xyspeedcheck;
	int waterlevel;
	vec3_t mins, maxs;
	vec3_t point;
	trace_t trace;

	if( state->type == ET_CORPSE ) {
		PlayerModelAnimationSet a;
		a.parts[ LOWER ] = ANIM_NONE;
		a.parts[ UPPER ] = ANIM_NONE;
		a.parts[ HEAD ] = ANIM_NONE;
		return a;
	}

	GS_BBoxForEntityState( state, mins, maxs );

	// determine if player is at ground, for walking or falling
	// this is not like having groundEntity, we are more generous with
	// the tracing size here to include small steps
	point[0] = state->origin[0];
	point[1] = state->origin[1];
	point[2] = state->origin[2] - ( 1.6 * STEPSIZE );
	gs.api.Trace( &trace, state->origin, mins, maxs, point, state->number, MASK_PLAYERSOLID, 0 );
	if( trace.ent == -1 || ( trace.fraction < 1.0f && !ISWALKABLEPLANE( &trace.plane ) && !trace.startsolid ) ) {
		moveflags |= ANIMMOVE_AIR;
	}

	// crouching : fixme? : it assumes the entity is using the player box sizes
	if( VectorCompare( maxs, playerbox_crouch_maxs ) ) {
		moveflags |= ANIMMOVE_DUCK;
	}

	// find out the water level
	waterlevel = GS_WaterLevel( state, mins, maxs );
	if( waterlevel >= 2 || ( waterlevel && ( moveflags & ANIMMOVE_AIR ) ) ) {
		moveflags |= ANIMMOVE_SWIM;
	}

	//find out what are the base movements the model is doing

	hvel[0] = velocity[0];
	hvel[1] = velocity[1];
	hvel[2] = 0;
	xyspeedcheck = VectorNormalize2( hvel, movedir );
	if( xyspeedcheck > WALKEPSILON ) {
		Matrix3_FromAngles( tv( 0, state->angles[YAW], 0 ), viewaxis );

		// if it's moving to where is looking, it's moving forward
		if( DotProduct( movedir, &viewaxis[AXIS_RIGHT] ) > MOVEDIREPSILON ) {
			moveflags |= ANIMMOVE_RIGHT;
		} else if( -DotProduct( movedir, &viewaxis[AXIS_RIGHT] ) > MOVEDIREPSILON ) {
			moveflags |= ANIMMOVE_LEFT;
		}
		if( DotProduct( movedir, &viewaxis[AXIS_FORWARD] ) > MOVEDIREPSILON ) {
			moveflags |= ANIMMOVE_FRONT;
		} else if( -DotProduct( movedir, &viewaxis[AXIS_FORWARD] ) > MOVEDIREPSILON ) {
			moveflags |= ANIMMOVE_BACK;
		}

		if( xyspeedcheck > RUNEPSILON ) {
			moveflags |= ANIMMOVE_RUN;
		} else if( xyspeedcheck > WALKEPSILON ) {
			moveflags |= ANIMMOVE_WALK;
		}
	}

	PlayerModelAnimationSet a;
	a.parts[ LOWER ] = CG_MoveFlagsToLowerAnimation( moveflags );
	a.parts[ UPPER ] = CG_MoveFlagsToUpperAnimation( moveflags, state->weapon );
	a.parts[ HEAD ] = ANIM_NONE;
	return a;
}

static float PositiveMod( float x, float y ) {
        float res = fmodf( x, y );
        if( res < 0 )
                res += y;
        return res;
}

static float GetAnimationTime( const PlayerModelMetadata * metadata, int64_t curTime, animstate_t state, bool loop ) {
	if( state.anim == ANIM_NONE )
		return -1.0f;

	PlayerModelMetadata::AnimationClip clip = metadata->clips[ state.anim ];

	float t = Max2( 0.0f, ( curTime - state.startTimestamp ) / 1000.0f );
	if( loop ) {
		if( t > clip.loop_from ) {
			float loop_t = PositiveMod( t - clip.loop_from, clip.duration - clip.loop_from );
			t = clip.loop_from + loop_t;
		}
	}
	else if( t >= clip.duration ) {
		return -1.0f;
	}

	return clip.start_time + t;
}

/*
*CG_PModel_AnimToFrame
*
* BASE_CHANEL plays continuous animations forced to loop.
* if the same animation is received twice it will *not* restart
* but continue looping.
*
* EVENT_CHANNEL overrides base channel and plays until
* the animation is finished. Then it returns to base channel.
* If an animation is received twice, it will be restarted.
* If an event channel animation has a loop setting, it will
* continue playing it until a new event chanel animation
* is fired.
*/
static void CG_GetAnimationTimes( pmodel_t * pmodel, int64_t curTime, float * lower_time, float * upper_time ) {
	float times[ PMODEL_PARTS ];

	for( int i = LOWER; i < PMODEL_PARTS; i++ ) {
		for( int channel = BASE_CHANNEL; channel < PLAYERANIM_CHANNELS; channel++ ) {
			animstate_t *currAnim = &pmodel->animState.curAnims[i][channel];

			// see if there are new animations to be played
			if( pmodel->animState.pending[channel].parts[i] != ANIM_NONE ) {
				if( channel == EVENT_CHANNEL || ( channel == BASE_CHANNEL && pmodel->animState.pending[channel].parts[i] != currAnim->anim ) ) {
					currAnim->anim = pmodel->animState.pending[channel].parts[i];
					currAnim->startTimestamp = curTime;
				}

				pmodel->animState.pending[channel].parts[i] = ANIM_NONE;
			}
		}

		times[ i ] = GetAnimationTime( pmodel->metadata, curTime, pmodel->animState.curAnims[ i ][ EVENT_CHANNEL ], false );
		if( times[ i ] == -1.0f ) {
			pmodel->animState.curAnims[ i ][ EVENT_CHANNEL ].anim = ANIM_NONE;
			times[ i ] = GetAnimationTime( pmodel->metadata, curTime, pmodel->animState.curAnims[ i ][ BASE_CHANNEL ], true );
		}
	}

	*lower_time = times[ LOWER ];
	*upper_time = times[ UPPER ];
}

void CG_PModel_ClearEventAnimations( int entNum ) {
	pmodel_animationstate_t & animState = cg_entPModels[ entNum ].animState;
	for( int i = LOWER; i < PMODEL_PARTS; i++ ) {
		animState.curAnims[ i ][ EVENT_CHANNEL ].anim = ANIM_NONE;
		animState.pending[ EVENT_CHANNEL ].parts[ i ] = ANIM_NONE;
	}
}

void CG_PModel_AddAnimation( int entNum, int loweranim, int upperanim, int headanim, int channel ) {
	PlayerModelAnimationSet & pending = cg_entPModels[entNum].animState.pending[ channel ];
	if( loweranim != ANIM_NONE )
		pending.parts[ LOWER ] = loweranim;
	if( upperanim != ANIM_NONE )
		pending.parts[ UPPER ] = upperanim;
	if( headanim != ANIM_NONE )
		pending.parts[ HEAD ] = headanim;
}


//======================================================================
//							player model
//======================================================================


void CG_PModel_LeanAngles( centity_t *cent, pmodel_t *pmodel ) {
	mat3_t axis;
	vec3_t hvelocity;
	float speed, front, side, aside, scale;
	vec3_t leanAngles[PMODEL_PARTS];
	int i, j;

	memset( leanAngles, 0, sizeof( leanAngles ) );

	hvelocity[0] = cent->animVelocity[0];
	hvelocity[1] = cent->animVelocity[1];
	hvelocity[2] = 0;

	scale = 0.04f;

	if( ( speed = VectorLengthFast( hvelocity ) ) * scale > 1.0f ) {
		AnglesToAxis( tv( 0, cent->current.angles[YAW], 0 ), axis );

		front = scale * DotProduct( hvelocity, &axis[AXIS_FORWARD] );
		if( front < -0.1 || front > 0.1 ) {
			leanAngles[LOWER][PITCH] += front;
			leanAngles[UPPER][PITCH] -= front * 0.25;
			leanAngles[HEAD][PITCH] -= front * 0.5;
		}

		aside = ( front * 0.001f ) * cent->yawVelocity;

		if( aside ) {
			float asidescale = 75;
			leanAngles[LOWER][ROLL] -= aside * 0.5 * asidescale;
			leanAngles[UPPER][ROLL] += aside * 1.75 * asidescale;
			leanAngles[HEAD][ROLL] -= aside * 0.35 * asidescale;
		}

		side = scale * DotProduct( hvelocity, &axis[AXIS_RIGHT] );

		if( side < -1 || side > 1 ) {
			leanAngles[LOWER][ROLL] -= side * 0.5;
			leanAngles[UPPER][ROLL] += side * 0.5;
			leanAngles[HEAD][ROLL] += side * 0.25;
		}

		clamp( leanAngles[LOWER][PITCH], -45, 45 );
		clamp( leanAngles[LOWER][ROLL], -15, 15 );

		clamp( leanAngles[UPPER][PITCH], -45, 45 );
		clamp( leanAngles[UPPER][ROLL], -20, 20 );

		clamp( leanAngles[HEAD][PITCH], -45, 45 );
		clamp( leanAngles[HEAD][ROLL], -20, 20 );
	}

	for( j = LOWER; j < PMODEL_PARTS; j++ ) {
		for( i = 0; i < 3; i++ )
			pmodel->angles[i][j] = AngleNormalize180( pmodel->angles[i][j] + leanAngles[i][j] );
	}
}

/*
* CG_UpdatePModelAnimations
* It's better to delay this set up until the other entities are linked, so they
* can be detected as groundentities by the animation checks
*/
static void CG_UpdatePModelAnimations( centity_t *cent ) {
	PlayerModelAnimationSet a = CG_GetBaseAnims( &cent->current, cent->animVelocity );
	CG_PModel_AddAnimation( cent->current.number, a.parts[LOWER], a.parts[UPPER], a.parts[HEAD], BASE_CHANNEL );
}

/*
* CG_UpdatePlayerModelEnt
* Called each new serverframe
*/
void CG_UpdatePlayerModelEnt( centity_t *cent ) {
	pmodel_t *pmodel;

	// start from clean
	memset( &cent->ent, 0, sizeof( cent->ent ) );
	cent->ent.scale = 1.0f;
	cent->ent.rtype = RT_MODEL;
	cent->ent.renderfx = cent->renderfx;

	pmodel = &cg_entPModels[cent->current.number];
	CG_PModelForCentity( cent, &pmodel->metadata, &pmodel->skin );

	CG_TeamColorForEntity( cent->current.number, cent->ent.shaderRGBA );

	Vector4Set( cent->outlineColor, 0, 0, 0, 255 );

	if( cg_raceGhosts->integer && !ISVIEWERENTITY( cent->current.number ) && GS_RaceGametype() ) {
		cent->effects &= ~EF_OUTLINE;
		cent->effects |= EF_RACEGHOST;
	} else {
		if( cg_outlinePlayers->integer ) {
			cent->effects |= EF_OUTLINE; // add EF_OUTLINE to players
		} else {
			cent->effects &= ~EF_OUTLINE;
		}
	}

	// fallback
	if( !pmodel->metadata || !pmodel->skin ) {
		pmodel->metadata = cgs.basePModelInfo;
		pmodel->skin = cgs.baseSkin;
	}

	// Spawning (teleported bit) forces nobacklerp and the interruption of EVENT_CHANNEL animations
	if( cent->current.teleported ) {
		CG_PModel_ClearEventAnimations( cent->current.number );
	}

	// update parts rotation angles
	for( int i = LOWER; i < PMODEL_PARTS; i++ )
		VectorCopy( pmodel->angles[i], pmodel->oldangles[i] );

	if( cent->current.type == ET_CORPSE ) {
		VectorClear( cent->animVelocity );
		cent->yawVelocity = 0;
	} else {
		// update smoothed velocities used for animations and leaning angles
		int count;
		float adelta;

		// rotational yaw velocity
		adelta = AngleDelta( cent->current.angles[YAW], cent->prev.angles[YAW] );
		clamp( adelta, -35, 35 );

		// smooth a velocity vector between the last snaps
		cent->lastVelocities[cg.frame.serverFrame & 3][0] = cent->velocity[0];
		cent->lastVelocities[cg.frame.serverFrame & 3][1] = cent->velocity[1];
		cent->lastVelocities[cg.frame.serverFrame & 3][2] = 0;
		cent->lastVelocities[cg.frame.serverFrame & 3][3] = adelta;
		cent->lastVelocitiesFrames[cg.frame.serverFrame & 3] = cg.frame.serverFrame;

		count = 0;
		VectorClear( cent->animVelocity );
		cent->yawVelocity = 0;
		for( int i = cg.frame.serverFrame; ( i >= 0 ) && ( count < 3 ) && ( i == cent->lastVelocitiesFrames[i & 3] ); i-- ) {
			count++;
			cent->animVelocity[0] += cent->lastVelocities[i & 3][0];
			cent->animVelocity[1] += cent->lastVelocities[i & 3][1];
			cent->animVelocity[2] += cent->lastVelocities[i & 3][2];
			cent->yawVelocity += cent->lastVelocities[i & 3][3];
		}

		// safety/static code analysis check
		if( count == 0 ) {
			count = 1;
		}

		VectorScale( cent->animVelocity, 1.0f / (float)count, cent->animVelocity );
		cent->yawVelocity /= (float)count;


		//
		// Calculate angles for each model part
		//

		// lower has horizontal direction, and zeroes vertical
		pmodel->angles[LOWER][PITCH] = 0;
		pmodel->angles[LOWER][YAW] = cent->current.angles[YAW];
		pmodel->angles[LOWER][ROLL] = 0;

		// upper marks vertical direction (total angle, so it fits aim)
		if( cent->current.angles[PITCH] > 180 ) {
			pmodel->angles[UPPER][PITCH] = ( -360 + cent->current.angles[PITCH] );
		} else {
			pmodel->angles[UPPER][PITCH] = cent->current.angles[PITCH];
		}

		pmodel->angles[UPPER][YAW] = 0;
		pmodel->angles[UPPER][ROLL] = 0;

		// head adds a fraction of vertical angle again
		if( cent->current.angles[PITCH] > 180 ) {
			pmodel->angles[HEAD][PITCH] = ( -360 + cent->current.angles[PITCH] ) / 3;
		} else {
			pmodel->angles[HEAD][PITCH] = cent->current.angles[PITCH] / 3;
		}

		pmodel->angles[HEAD][YAW] = 0;
		pmodel->angles[HEAD][ROLL] = 0;

		CG_PModel_LeanAngles( cent, pmodel );
	}


	// Spawning (teleported bit) forces nobacklerp and the interruption of EVENT_CHANNEL animations
	if( cent->current.teleported ) {
		for( int i = LOWER; i < PMODEL_PARTS; i++ )
			VectorCopy( pmodel->angles[i], pmodel->oldangles[i] );
	}

	CG_UpdatePModelAnimations( cent );
}

static Quaternion EulerAnglesToQuaternion( EulerDegrees3 angles ) {
	float cp = cosf( DEG2RAD( angles.pitch ) * 0.5f );
	float sp = sinf( DEG2RAD( angles.pitch ) * 0.5f );
	float cy = cosf( DEG2RAD( angles.yaw ) * 0.5f );
	float sy = sinf( DEG2RAD( angles.yaw ) * 0.5f );
	float cr = cosf( DEG2RAD( angles.roll ) * 0.5f );
	float sr = sinf( DEG2RAD( angles.roll ) * 0.5f );

	return Quaternion(
		cp * cy * sr - sp * sy * cr,
		cp * sy * cr + sp * cy * sr,
		sp * cy * cr - cp * sy * sr,
		cp * cy * cr + sp * sy * sr
	);
}

static Mat4 QFToMat4( const mat4_t qf ) {
	Mat4 m;
	memcpy( m.ptr(), qf, sizeof( m ) );
	return m;
}

static orientation_t TransformTag( const model_t * model, const MatrixPalettes & pose, const PlayerModelMetadata::Tag & tag ) {
	Mat4 transform = QFToMat4( model->transform ) * pose.joint_poses[ tag.joint_idx ] * tag.transform;
	orientation_t o;

	o.axis[ 0 ] = transform.col0.x;
	o.axis[ 1 ] = transform.col0.y;
	o.axis[ 2 ] = transform.col0.z;
	o.axis[ 3 ] = transform.col1.x;
	o.axis[ 4 ] = transform.col1.y;
	o.axis[ 5 ] = transform.col1.z;
	o.axis[ 6 ] = transform.col2.x;
	o.axis[ 7 ] = transform.col2.y;
	o.axis[ 8 ] = transform.col2.z;

	o.origin[ 0 ] = transform.col3.x;
	o.origin[ 1 ] = transform.col3.y;
	o.origin[ 2 ] = transform.col3.z;

	return o;
}

void CG_AddPModel( centity_t *cent ) {
	pmodel_t * pmodel = &cg_entPModels[cent->current.number];
	const PlayerModelMetadata * meta = pmodel->metadata;

	// if viewer model, and casting shadows, offset the entity to predicted player position
	// for view and shadow accuracy

	if( ISVIEWERENTITY( cent->current.number ) ) {
		vec3_t org;

		if( cg.view.playerPrediction ) {
			float backlerp = 1.0f - cg.lerpfrac;

			for( int i = 0; i < 3; i++ )
				org[i] = cg.predictedPlayerState.pmove.origin[i] - backlerp * cg.predictionError[i];

			CG_ViewSmoothPredictedSteps( org );

			vec3_t tmpangles;
			tmpangles[YAW] = cg.predictedPlayerState.viewangles[YAW];
			tmpangles[PITCH] = 0;
			tmpangles[ROLL] = 0;
			AnglesToAxis( tmpangles, cent->ent.axis );
		} else {
			VectorCopy( cent->ent.origin, org );
		}

		VectorCopy( org, cent->ent.origin );
		VectorCopy( org, cent->ent.origin2 );
	}

	float lower_time, upper_time;
	CG_GetAnimationTimes( pmodel, cg.time, &lower_time, &upper_time );
	Span< TRS > lower = R_SampleAnimation( cls.frame_arena, meta->model, lower_time );
	Span< TRS > upper = R_SampleAnimation( cls.frame_arena, meta->model, upper_time );
	R_MergeLowerUpperPoses( lower, upper, meta->model, meta->upper_root_joint );

	// add skeleton effects (pose is unmounted yet)
	if( cent->current.type != ET_CORPSE ) {
		vec3_t tmpangles;
		// if it's our client use the predicted angles
		if( cg.view.playerPrediction && ISVIEWERENTITY( cent->current.number ) && ( (unsigned)cg.view.POVent == cgs.playerNum + 1 ) ) {
			tmpangles[YAW] = cg.predictedPlayerState.viewangles[YAW];
			tmpangles[PITCH] = 0;
			tmpangles[ROLL] = 0;
		}
		else {
			// apply interpolated LOWER angles to entity
			for( int i = 0; i < 3; i++ ) {
				tmpangles[i] = LerpAngle( pmodel->oldangles[LOWER][i], pmodel->angles[LOWER][i], cg.lerpfrac );
			}
		}

		AnglesToAxis( tmpangles, cent->ent.axis );

		// apply UPPER and HEAD angles to rotator joints
		// also add rotations from velocity leaning
		{
			EulerDegrees3 angles;
			angles.pitch = LerpAngle( pmodel->oldangles[ UPPER ][ PITCH ], pmodel->angles[ UPPER ][ PITCH ], cg.lerpfrac ) / 2.0f;
			angles.yaw = LerpAngle( pmodel->oldangles[ UPPER ][ YAW ], pmodel->angles[ UPPER ][ YAW ], cg.lerpfrac ) / 2.0f;
			angles.roll = LerpAngle( pmodel->oldangles[ UPPER ][ ROLL ], pmodel->angles[ UPPER ][ ROLL ], cg.lerpfrac ) / 2.0f;

			Quaternion q = EulerAnglesToQuaternion( angles );
			lower[ meta->upper_rotator_joints[ 0 ] ].rotation *= q;
			lower[ meta->upper_rotator_joints[ 1 ] ].rotation *= q;
		}

		{
			EulerDegrees3 angles;
			angles.pitch = LerpAngle( pmodel->oldangles[ HEAD ][ PITCH ], pmodel->angles[ HEAD ][ PITCH ], cg.lerpfrac );
			angles.yaw = LerpAngle( pmodel->oldangles[ HEAD ][ YAW ], pmodel->angles[ HEAD ][ YAW ], cg.lerpfrac );
			angles.roll = LerpAngle( pmodel->oldangles[ HEAD ][ ROLL ], pmodel->angles[ HEAD ][ ROLL ], cg.lerpfrac );

			lower[ meta->head_rotator_joint ].rotation *= EulerAnglesToQuaternion( angles );
		}
	}

	// Add playermodel ent
	cent->ent.scale = 1.0f;
	cent->ent.rtype = RT_MODEL;
	cent->ent.model = meta->model;
	cent->ent.customShader = NULL;
	cent->ent.customSkin = pmodel->skin;
	cent->ent.renderfx |= RF_NOSHADOW;
	cent->ent.pose = R_ComputeMatrixPalettes( cls.frame_arena, meta->model, lower );

	if( !( cent->renderfx & RF_NOSHADOW ) ) {
		CG_AllocPlayerShadow( cent->current.number, cent->ent.origin, playerbox_stand_mins, playerbox_stand_maxs );
	}

	if( !( cent->effects & EF_RACEGHOST ) ) {
		CG_AddCentityOutLineEffect( cent );
		CG_AddEntityToScene( &cent->ent );
	}

	CG_AddShellEffects( &cent->ent, cent->effects );

	// add teleporter sfx if needed
	CG_PModel_SpawnTeleportEffect( cent );

	// add weapon model
	if( cent->current.weapon ) {
		orientation_t tag_weapon = TransformTag( meta->model, cent->ent.pose, meta->tag_weapon );
		CG_AddWeaponOnTag( &cent->ent, &tag_weapon, cent->current.weapon, cent->effects,
			&pmodel->projectionSource, pmodel->flash_time, pmodel->barrel_time );
	}

	// add backpack/hat
	if( cent->current.modelindex2 ) {
		PlayerModelMetadata::Tag tag = meta->tag_backpack;
		if( cent->current.effects & EF_HAT )
			tag = meta->tag_head;
		orientation_t o = TransformTag( meta->model, cent->ent.pose, tag );
		CG_AddLinkedModel( cent, &o );
	}
}
