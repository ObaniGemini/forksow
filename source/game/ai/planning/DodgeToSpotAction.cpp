#include "PlanningLocal.h"
#include "../bot.h"

void BotDodgeToSpotActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	self->ai->botRef->SetNavTarget( &navTarget );
	timeoutAt = level.time + Hazard::TIMEOUT;
	self->ai->botRef->GetMiscTactics().PreferAttackRatherThanRun();
}

void BotDodgeToSpotActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	self->ai->botRef->ResetNavTarget();
}

AiBaseActionRecord::Status BotDodgeToSpotActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	// If the bot has reached the spot, consider the action completed
	// (use a low threshold because dodging is a precise movement)
	if( ( navTarget.Origin() - self->s.origin ).SquaredLength() < 16 * 16 ) {
		return COMPLETED;
	}

	// Return INVALID if has not reached the spot when the action timed out
	return timeoutAt > level.time ? VALID : INVALID;
}

PlannerNode *BotDodgeToSpotAction::TryApply( const WorldState &worldState ) {
	if( worldState.PotentialHazardDamageVar().Ignore() ) {
		Debug( "Potential hazard damage is ignored in the given world state\n" );
		return nullptr;
	}

#ifndef _DEBUG
	// Sanity check
	if( worldState.HazardHitPointVar().Ignore() ) {
		AI_FailWith( "BotDodgeToSpotAction::TryApply()", "Hazard hit point is ignored in the given world state\n" );
	}
	if( worldState.HazardDirectionVar().Ignore() ) {
		AI_FailWith( "BotDodgeToSpotAction::TryApply()", "Hazard direction is ignored in the given world state\n" );
	}
#endif

	constexpr float squareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - self->s.origin ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	if( worldState.DodgeHazardSpotVar().Ignore() ) {
		Debug( "Spot for dodging a hazard is ignored in the given world state, can't dodge\n" );
		return nullptr;
	}

	const Vec3 spotOrigin = worldState.DodgeHazardSpotVar().Value();
	int travelTimeMillis = self->ai->botRef->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), spotOrigin );
	if( !travelTimeMillis ) {
		Debug( "Warning: can't find travel time from the bot origin to the spot origin in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( self, spotOrigin ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = travelTimeMillis;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( spotOrigin );
	plannerNode.WorldState().PotentialHazardDamageVar().SetIgnore( true );
	plannerNode.WorldState().HazardHitPointVar().SetIgnore( true );
	plannerNode.WorldState().HazardDirectionVar().SetIgnore( true );
	plannerNode.WorldState().HasReactedToHazardVar().SetIgnore( false ).SetValue( true );

	return plannerNode.PrepareActionResult();
}
