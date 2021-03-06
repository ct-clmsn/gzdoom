//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//		Base class for effects on sectors.
//		[RH] Created this class hierarchy.
//
//-----------------------------------------------------------------------------

#include "dsectoreffect.h"
#include "gi.h"
#include "p_local.h"
#include "g_levellocals.h"
#include "p_3dmidtex.h"
#include "r_data/r_interpolate.h"
#include "statnums.h"
#include "serializer.h"
#include "doomstat.h"
#include "vm.h"

IMPLEMENT_CLASS(DSectorEffect, false, false)

DSectorEffect::DSectorEffect ()
: DThinker(STAT_SECTOREFFECT)
{
	m_Sector = NULL;
}

void DSectorEffect::OnDestroy()
{
	if (m_Sector)
	{
		if (m_Sector->floordata == this)
		{
			m_Sector->floordata = NULL;
		}
		if (m_Sector->ceilingdata == this)
		{
			m_Sector->ceilingdata = NULL;
		}
		if (m_Sector->lightingdata == this)
		{
			m_Sector->lightingdata = NULL;
		}
	}
	Super::OnDestroy();
}

DSectorEffect::DSectorEffect (sector_t *sector)
	: DThinker(STAT_SECTOREFFECT)
{
	m_Sector = sector;
}

void DSectorEffect::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc("sector", m_Sector);
}

DEFINE_FIELD(DSectorEffect, m_Sector)


IMPLEMENT_CLASS(DMover, true, true)

IMPLEMENT_POINTERS_START(DMover)
	IMPLEMENT_POINTER(interpolation)
IMPLEMENT_POINTERS_END

DMover::DMover (sector_t *sector)
	: DSectorEffect (sector)
{
	interpolation = NULL;
}

void DMover::OnDestroy()
{
	StopInterpolation();
	Super::OnDestroy();
}

void DMover::Serialize(FSerializer &arc)
{
	Super::Serialize (arc);
	arc("interpolation", interpolation);
}

void DMover::StopInterpolation(bool force)
{
	if (interpolation != NULL)
	{
		interpolation->DelRef(force);
		interpolation = NULL;
	}
}

IMPLEMENT_CLASS(DMovingFloor, true, false)


DMovingFloor::DMovingFloor (sector_t *sector)
	: DMover (sector)
{
	sector->floordata = this;
	interpolation = sector->SetInterpolation(sector_t::FloorMove, true);
}

IMPLEMENT_CLASS(DMovingCeiling, true, false)


DMovingCeiling::DMovingCeiling (sector_t *sector, bool interpolate)
	: DMover (sector)
{
	sector->ceilingdata = this;
	if (interpolate) interpolation = sector->SetInterpolation(sector_t::CeilingMove, true);
}

bool sector_t::MoveAttached(int crush, double move, int floorOrCeiling, bool resetfailed, bool instant)
{
	if (!P_Scroll3dMidtex(this, crush, move, !!floorOrCeiling, instant) && resetfailed)
	{
		P_Scroll3dMidtex(this, crush, -move, !!floorOrCeiling, instant);
		return false;
	}
	if (!P_MoveLinkedSectors(this, crush, move, !!floorOrCeiling, instant) && resetfailed)
	{
		P_MoveLinkedSectors(this, crush, -move, !!floorOrCeiling, instant);
		P_Scroll3dMidtex(this, crush, -move, !!floorOrCeiling, instant);
		return false;
	}
	return true;
}

//
// Move a plane (floor or ceiling) and check for crushing
// [RH] Crush specifies the actual amount of crushing damage inflictable.
//		(Use -1 to prevent it from trying to crush)
//		dest is the desired d value for the plane
//
EMoveResult sector_t::MoveFloor(double speed, double dest, int crush, int direction, bool hexencrush, bool instant)
{
	bool	 	flag;
	double 	lastpos;
	double		movedest;
	double		move;
	//double		destheight;	//jff 02/04/98 used to keep floors/ceilings
							// from moving thru each other
	lastpos = floorplane.fD();
	switch (direction)
	{
	case -1:
		// DOWN
		movedest = floorplane.GetChangedHeight(-speed);
		if (movedest >= dest)
		{
			move = floorplane.HeightDiff(lastpos, dest);

			if (!MoveAttached(crush, move, 0, true, instant)) return EMoveResult::crushed;

			floorplane.setD(dest);
			flag = P_ChangeSector(this, crush, move, 0, false, instant);
			if (flag)
			{
				floorplane.setD(lastpos);
				P_ChangeSector(this, crush, -move, 0, true, instant);
				MoveAttached(crush, -move, 0, false, instant);
			}
			else
			{
				ChangePlaneTexZ(sector_t::floor, move);
				AdjustFloorClip();
			}
			return EMoveResult::pastdest;
		}
		else
		{
			if (!MoveAttached(crush, -speed, 0, true, instant)) return EMoveResult::crushed;

			floorplane.setD(movedest);

			flag = P_ChangeSector(this, crush, -speed, 0, false, instant);
			if (flag)
			{
				floorplane.setD(lastpos);
				P_ChangeSector(this, crush, speed, 0, true, instant);
				MoveAttached(crush, speed, 0, false, instant);
				return EMoveResult::crushed;
			}
			else
			{
				ChangePlaneTexZ(sector_t::floor, floorplane.HeightDiff(lastpos));
				AdjustFloorClip();
			}
		}
		break;

	case 1:
		// UP
		// jff 02/04/98 keep floor from moving thru ceilings
		// [RH] not so easy with arbitrary planes
		//destheight = (dest < ceilingheight) ? dest : ceilingheight;
		if (!ceilingplane.isSlope() && !floorplane.isSlope() &&
			!PortalIsLinked(sector_t::ceiling) &&
			(!(i_compatflags2 & COMPATF2_FLOORMOVE) && -dest > ceilingplane.fD()))
		{
			dest = -ceilingplane.fD();
		}

		movedest = floorplane.GetChangedHeight(speed);

		if (movedest <= dest)
		{
			move = floorplane.HeightDiff(lastpos, dest);

			if (!MoveAttached(crush, move, 0, true, instant)) return EMoveResult::crushed;

			floorplane.setD(dest);

			flag = P_ChangeSector(this, crush, move, 0, false, instant);
			if (flag)
			{
				floorplane.setD(lastpos);
				P_ChangeSector(this, crush, -move, 0, true, instant);
				MoveAttached(crush, -move, 0, false, instant);
			}
			else
			{
				ChangePlaneTexZ(sector_t::floor, move);
				AdjustFloorClip();
			}
			return EMoveResult::pastdest;
		}
		else
		{
			if (!MoveAttached(crush, speed, 0, true, instant)) return EMoveResult::crushed;

			floorplane.setD(movedest);

			// COULD GET CRUSHED
			flag = P_ChangeSector(this, crush, speed, 0, false, instant);
			if (flag)
			{
				if (crush >= 0 && !hexencrush)
				{
					ChangePlaneTexZ(sector_t::floor, floorplane.HeightDiff(lastpos));
					AdjustFloorClip();
					return EMoveResult::crushed;
				}
				floorplane.setD(lastpos);
				P_ChangeSector(this, crush, -speed, 0, true, instant);
				MoveAttached(crush, -speed, 0, false, instant);
				return EMoveResult::crushed;
			}
			ChangePlaneTexZ(sector_t::floor, floorplane.HeightDiff(lastpos));
			AdjustFloorClip();
		}
		break;
	}
	return EMoveResult::ok;
}

DEFINE_ACTION_FUNCTION(_Sector, MoveFloor)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_FLOAT(speed);
	PARAM_FLOAT(dest);
	PARAM_INT(crush);
	PARAM_INT(dir);
	PARAM_BOOL(hex);
	PARAM_BOOL_DEF(inst);
	ACTION_RETURN_INT((int)self->MoveFloor(speed, dest, crush, dir, hex, inst));
}


EMoveResult sector_t::MoveCeiling(double speed, double dest, int crush, int direction, bool hexencrush)
{
	bool	 	flag;
	double 	lastpos;
	double		movedest;
	double		move;
	//double		destheight;	//jff 02/04/98 used to keep floors/ceilings
	// from moving thru each other

	lastpos = ceilingplane.fD();
	switch (direction)
	{
	case -1:
		// DOWN
		// jff 02/04/98 keep ceiling from moving thru floors
		// [RH] not so easy with arbitrary planes
		//destheight = (dest > floorheight) ? dest : floorheight;
		if (!ceilingplane.isSlope() && !floorplane.isSlope() &&
			!PortalIsLinked(sector_t::floor) &&
			(!(i_compatflags2 & COMPATF2_FLOORMOVE) && dest < -floorplane.fD()))
		{
			dest = -floorplane.fD();
		}
		movedest = ceilingplane.GetChangedHeight (-speed);
		if (movedest <= dest)
		{
			move = ceilingplane.HeightDiff (lastpos, dest);

			if (!MoveAttached(crush, move, 1, true)) return EMoveResult::crushed;

			ceilingplane.setD(dest);
			flag = P_ChangeSector (this, crush, move, 1, false);

			if (flag)
			{
				ceilingplane.setD(lastpos);
				P_ChangeSector (this, crush, -move, 1, true);
				MoveAttached(crush, -move, 1, false);
			}
			else
			{
				ChangePlaneTexZ(sector_t::ceiling, move);
			}
			return EMoveResult::pastdest;
		}
		else
		{
			if (!MoveAttached(crush, -speed, 1, true)) return EMoveResult::crushed;

			ceilingplane.setD(movedest);

			// COULD GET CRUSHED
			flag = P_ChangeSector (this, crush, -speed, 1, false);
			if (flag)
			{
				if (crush >= 0 && !hexencrush)
				{
					ChangePlaneTexZ(sector_t::ceiling, ceilingplane.HeightDiff (lastpos));
					return EMoveResult::crushed;
				}
				ceilingplane.setD(lastpos);
				P_ChangeSector (this, crush, speed, 1, true);
				MoveAttached(crush, speed, 1, false);
				return EMoveResult::crushed;
			}
			ChangePlaneTexZ(sector_t::ceiling, ceilingplane.HeightDiff (lastpos));
		}
		break;
												
	case 1:
		// UP
		movedest = ceilingplane.GetChangedHeight (speed);
		if (movedest >= dest)
		{
			move = ceilingplane.HeightDiff (lastpos, dest);

			if (!MoveAttached(crush, move, 1, true)) return EMoveResult::crushed;

			ceilingplane.setD(dest);

			flag = P_ChangeSector (this, crush, move, 1, false);
			if (flag)
			{
				ceilingplane.setD(lastpos);
				P_ChangeSector (this, crush, move, 1, true);
				MoveAttached(crush, move, 1, false);
			}
			else
			{
				ChangePlaneTexZ(sector_t::ceiling, move);
			}
			return EMoveResult::pastdest;
		}
		else
		{
			if (!MoveAttached(crush, speed, 1, true)) return EMoveResult::crushed;

			ceilingplane.setD(movedest);

			flag = P_ChangeSector (this, crush, speed, 1, false);
			if (flag)
			{
				ceilingplane.setD(lastpos);
				P_ChangeSector (this, crush, -speed, 1, true);
				MoveAttached(crush, -speed, 1, false);
				return EMoveResult::crushed;
			}
			ChangePlaneTexZ(sector_t::ceiling, ceilingplane.HeightDiff (lastpos));
		}
		break;
	}
	return EMoveResult::ok;
}

DEFINE_ACTION_FUNCTION(_Sector, MoveCeiling)
{
	PARAM_SELF_STRUCT_PROLOGUE(sector_t);
	PARAM_FLOAT(speed);
	PARAM_FLOAT(dest);
	PARAM_INT(crush);
	PARAM_INT(dir);
	PARAM_BOOL(hex);
	ACTION_RETURN_INT((int)self->MoveCeiling(speed, dest, crush, dir, hex));
}
