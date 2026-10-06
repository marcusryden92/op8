//========= Opposing Force 2 ==================================================//
//
// Purpose: OF2: Knife, after SMOD Tactical's. Primary fire slashes; secondary
//			fire stabs, which is slower and does more damage.
//
//			SMOD's knife is one of its script-driven custom weapons
//			(scripts\weapon_custom25.txt there), so there was no code to copy.
//			The numbers below are that script's: range 80 for both attacks, slash
//			15 damage as DMG_SLASH, stab 50 as DMG_CLUB, and its hit times of
//			0.25 and 0.45 seconds into the swing, which is where the blade
//			crosses the middle of the screen in the viewmodel's animations.
//			Its push is cut down (of2_knife_push).
//
//			It is a crowbar underneath: the swing, the delayed hit and the hit
//			sounds are CBaseHLBludgeonWeapon's, and an NPC given one uses it
//			as a crowbar (sk_npc_dmg_crowbar).
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "weapon_crowbar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Also in cfg\skill.cfg. These have real defaults so the knife still cuts if that file is missing.
ConVar sk_plr_dmg_knife( "sk_plr_dmg_knife", "15" );
ConVar sk_plr_dmg_knife_stab( "sk_plr_dmg_knife_stab", "50" );

ConVar of2_knife_range( "of2_knife_range", "80", FCVAR_NONE, "How far the knife reaches." );
ConVar of2_knife_slash_delay( "of2_knife_slash_delay", "0.25", FCVAR_NONE, "Seconds from the start of a knife slash to the hit." );
ConVar of2_knife_slash_recover( "of2_knife_slash_recover", "0.05", FCVAR_NONE, "Seconds from a knife slash's hit to the next attack." );
ConVar of2_knife_stab_delay( "of2_knife_stab_delay", "0.45", FCVAR_NONE, "Seconds from the start of a knife stab to the hit." );
ConVar of2_knife_stab_recover( "of2_knife_stab_recover", "0.3", FCVAR_NONE, "Seconds from a knife stab's hit to the next attack." );

// Melee pushes by the damage, so at full push the stab shoves like five crowbar hits and
// sends bodies flying. 0.2 makes a stab push like one crowbar hit.
ConVar of2_knife_push( "of2_knife_push", "0.2", FCVAR_NONE, "How hard the knife pushes what it hits (bodies included), as a fraction of the usual melee push for its damage." );

// Stealth. About three metres: a knife going in is heard by someone standing next to it, who
// comes to look, and by nobody across the room. Also read by CAI_BaseNPC::OnTakeDamage_Alive.
ConVar of2_knife_noise( "of2_knife_noise", "120", FCVAR_NONE, "How far NPCs hear a knife hit, in units (the crowbar: 400, and a wounded NPC 1024)." );

// However the convars are set, an attack takes this long
#define KNIFE_MIN_REFIRE	0.1f

//-----------------------------------------------------------------------------
// CWeaponKnife
//-----------------------------------------------------------------------------
class CWeaponKnife : public CWeaponCrowbar
{
public:
	DECLARE_CLASS( CWeaponKnife, CWeaponCrowbar );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CWeaponKnife();

	void		PrimaryAttack( void );
	void		SecondaryAttack( void );
	bool		Deploy( void );

	float		GetRange( void )		{	return	of2_knife_range.GetFloat();	}
	float		GetFireRate( void );
	float		GetHitDelay();
	int			GetDamageType()			{	return	m_bStab ? DMG_CLUB : DMG_SLASH;	}
	float		GetDamageForActivity( Activity hitActivity );
	float		GetDamageForceScale()	{	return	MAX( of2_knife_push.GetFloat(), 0.0f );	}
	float		GetQuietHitRadius()		{	return	MAX( of2_knife_noise.GetFloat(), 1.0f );	}
	Activity	GetPrimaryAttackActivity( void )	{	return	m_bStab ? ACT_VM_SECONDARYATTACK : ACT_VM_HITCENTER;	}

	bool		SendWeaponAnim( int iActivity );
	void		WeaponSound( WeaponSound_t sound_type, float soundtime = 0.0f );
	void		AddViewKick( void );

private:
	// The attack in progress is a stab. The base class asks for the range, timing,
	// damage and animation without saying which attack it is for.
	bool		m_bStab;
};

IMPLEMENT_SERVERCLASS_ST( CWeaponKnife, DT_WeaponKnife )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_knife, CWeaponKnife );
PRECACHE_WEAPON_REGISTER( weapon_knife );

BEGIN_DATADESC( CWeaponKnife )

	DEFINE_FIELD( m_bStab, FIELD_BOOLEAN ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponKnife::CWeaponKnife( void )
{
	m_bStab = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWeaponKnife::Deploy( void )
{
	m_bStab = false;

	return BaseClass::Deploy();
}

//-----------------------------------------------------------------------------
// Purpose: Slash
//-----------------------------------------------------------------------------
void CWeaponKnife::PrimaryAttack( void )
{
	m_bStab = false;

	BaseClass::PrimaryAttack();

	// The base class times the other attack off the length of the animation
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + GetFireRate();
}

//-----------------------------------------------------------------------------
// Purpose: Stab
//-----------------------------------------------------------------------------
void CWeaponKnife::SecondaryAttack( void )
{
	m_bStab = true;

	// Past the crowbar, which has no secondary attack
	CBaseHLBludgeonWeapon::SecondaryAttack();

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + GetFireRate();
}

//-----------------------------------------------------------------------------
// Purpose: Time from the start of one attack to the start of the next
//-----------------------------------------------------------------------------
float CWeaponKnife::GetFireRate( void )
{
	float flRecover = m_bStab ? of2_knife_stab_recover.GetFloat() : of2_knife_slash_recover.GetFloat();

	return MAX( GetHitDelay() + flRecover, KNIFE_MIN_REFIRE );
}

//-----------------------------------------------------------------------------
// Purpose: Time from the start of an attack to its hit. At 0 the base class
//			hits at once instead.
//-----------------------------------------------------------------------------
float CWeaponKnife::GetHitDelay()
{
	float flDelay = m_bStab ? of2_knife_stab_delay.GetFloat() : of2_knife_slash_delay.GetFloat();

	return MAX( flDelay, 0.0f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float CWeaponKnife::GetDamageForActivity( Activity hitActivity )
{
	if ( ( GetOwner() != NULL ) && ( GetOwner()->IsPlayer() ) )
		return m_bStab ? sk_plr_dmg_knife_stab.GetFloat() : sk_plr_dmg_knife.GetFloat();

	return BaseClass::GetDamageForActivity( hitActivity );
}

//-----------------------------------------------------------------------------
// Purpose: The base class plays a different animation for a swing that will
//			miss. The viewmodel has one set, hit or miss.
//-----------------------------------------------------------------------------
bool CWeaponKnife::SendWeaponAnim( int iActivity )
{
	if ( iActivity == ACT_VM_MISSCENTER )
	{
		iActivity = ACT_VM_HITCENTER;
	}
	else if ( iActivity == ACT_VM_MISSCENTER2 )
	{
		iActivity = ACT_VM_SECONDARYATTACK;
	}

	return BaseClass::SendWeaponAnim( iActivity );
}

//-----------------------------------------------------------------------------
// Purpose: A stab that lands has its own sound ("special1" in the weapon script)
//-----------------------------------------------------------------------------
void CWeaponKnife::WeaponSound( WeaponSound_t sound_type, float soundtime /*= 0.0f*/ )
{
	if ( m_bStab && sound_type == MELEE_HIT )
	{
		sound_type = SPECIAL1;
	}

	BaseClass::WeaponSound( sound_type, soundtime );
}

//-----------------------------------------------------------------------------
// Purpose: Add in a view kick for this weapon: sideways for a slash, down for a stab
//-----------------------------------------------------------------------------
void CWeaponKnife::AddViewKick( void )
{
	CBasePlayer *pPlayer  = ToBasePlayer( GetOwner() );

	if ( pPlayer == NULL )
		return;

	QAngle punchAng;

	if ( m_bStab )
	{
		punchAng.x = random->RandomFloat( 1.5f, 2.5f );
		punchAng.y = random->RandomFloat( -0.5f, 0.5f );
	}
	else
	{
		punchAng.x = random->RandomFloat( -0.5f, 0.5f );
		punchAng.y = random->RandomFloat( 1.0f, 2.0f ) * ( random->RandomInt( 0, 1 ) ? 1.0f : -1.0f );
	}

	punchAng.z = 0.0f;

	pPlayer->ViewPunch( punchAng );
}
