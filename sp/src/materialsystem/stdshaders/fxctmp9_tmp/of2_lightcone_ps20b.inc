#include "shaderlib/cshader.h"
class of2_lightcone_ps20b_Static_Index
{
private:
	int m_nFLASHLIGHTDEPTHFILTERMODE;
#ifdef _DEBUG
	bool m_bFLASHLIGHTDEPTHFILTERMODE;
#endif
public:
	void SetFLASHLIGHTDEPTHFILTERMODE( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nFLASHLIGHTDEPTHFILTERMODE = i;
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = true;
#endif
	}
	void SetFLASHLIGHTDEPTHFILTERMODE( bool i )
	{
		m_nFLASHLIGHTDEPTHFILTERMODE = i ? 1 : 0;
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = true;
#endif
	}
public:
	of2_lightcone_ps20b_Static_Index( )
	{
#ifdef _DEBUG
		m_bFLASHLIGHTDEPTHFILTERMODE = false;
#endif // _DEBUG
		m_nFLASHLIGHTDEPTHFILTERMODE = 0;
	}
	int GetIndex()
	{
		// Asserts to make sure that we aren't using any skipped combinations.
		// Asserts to make sure that we are setting all of the combination vars.
#ifdef _DEBUG
		bool bAllStaticVarsDefined = m_bFLASHLIGHTDEPTHFILTERMODE;
		Assert( bAllStaticVarsDefined );
#endif // _DEBUG
		return ( 2 * m_nFLASHLIGHTDEPTHFILTERMODE ) + 0;
	}
};
#define shaderStaticTest_of2_lightcone_ps20b psh_forgot_to_set_static_FLASHLIGHTDEPTHFILTERMODE + 0
class of2_lightcone_ps20b_Dynamic_Index
{
private:
	int m_nSHADOWMAP;
#ifdef _DEBUG
	bool m_bSHADOWMAP;
#endif
public:
	void SetSHADOWMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nSHADOWMAP = i;
#ifdef _DEBUG
		m_bSHADOWMAP = true;
#endif
	}
	void SetSHADOWMAP( bool i )
	{
		m_nSHADOWMAP = i ? 1 : 0;
#ifdef _DEBUG
		m_bSHADOWMAP = true;
#endif
	}
public:
	of2_lightcone_ps20b_Dynamic_Index()
	{
#ifdef _DEBUG
		m_bSHADOWMAP = false;
#endif // _DEBUG
		m_nSHADOWMAP = 0;
	}
	int GetIndex()
	{
		// Asserts to make sure that we aren't using any skipped combinations.
		// Asserts to make sure that we are setting all of the combination vars.
#ifdef _DEBUG
		bool bAllDynamicVarsDefined = m_bSHADOWMAP;
		Assert( bAllDynamicVarsDefined );
#endif // _DEBUG
		return ( 1 * m_nSHADOWMAP ) + 0;
	}
};
#define shaderDynamicTest_of2_lightcone_ps20b psh_forgot_to_set_dynamic_SHADOWMAP + 0
