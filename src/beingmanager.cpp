/*
 *  The Mana World
 *  Copyright (C) 2004  The Mana World Development Team
 *
 *  This file is part of The Mana World.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "beingmanager.h"

#include "localplayer.h"
#include "monster.h"
#include "npc.h"
#include "player.h"
#include "map.h"
#include "game.h"

#ifdef TMWSERV_SUPPORT
#include "net/tmwserv/protocol.h"
#endif

#include "utils/dtor.h"

#include <cassert>

class FindBeingFunctor
{
public:
	bool operator()(Being *being)
	{
		Uint16 other_y = y + ((being->getType() == Being::NPC) ? 1 : 0);
#ifdef TMWSERV_SUPPORT
		const Vector &pos = being->getPosition();
		return ((int) pos.x / 32 == x &&
				((int) pos.y / 32 == y || (int) pos.y / 32 == other_y) &&
#else
		return (being->mX == x && (being->mY == y || being->mY == other_y) &&
#endif
				being->mAction != Being::DEAD && (type == Being::UNKNOWN
				|| being->getType() == type));
	}

	Uint16 x, y;
	Being::Type type;
} beingFinder;

BeingManager::BeingManager()
{
}

BeingManager::~BeingManager()
{
	clear();
}

void BeingManager::setMap(Map *map)
{
	mMap = map;
	if (player_node)
		player_node->setMap(map);
}

void BeingManager::setPlayer(LocalPlayer *player)
{
	player_node = player;
	mBeings.push_back(player);
}

Being *BeingManager::createBeing(int id, Being::Type type, int subtype)
{
	Being *being;

	switch (type)
	{
	case Being::PLAYER:
		being = new Player(id, subtype, mMap);
		break;
	case Being::NPC:
		being = new NPC(id, subtype, mMap);
		break;
	case Being::MONSTER:
		being = new Monster(id, subtype, mMap);
		break;
	case Being::UNKNOWN:
		being = new Being(id, subtype, mMap);
		break;
	default:
		assert(false);
	}

	mBeings.push_back(being);
	return being;
}

void BeingManager::destroyBeing(Being *being)
{
	mBeings.remove(being);
	delete being;
}

Being *BeingManager::findBeing(int id) const
{
	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
		if (being->getId() == id)
			return being;
	}
	return NULL;
}

Being *BeingManager::findBeing(int x, int y, Being::Type type) const
{
	beingFinder.x = x;
	beingFinder.y = y;
	beingFinder.type = type;

	Beings::const_iterator i = find_if(mBeings.begin(), mBeings.end(),
			beingFinder);

	return (i == mBeings.end()) ? NULL : *i;
}

Being *BeingManager::findBeingByPixel(int x, int y) const
{
	Beings::const_iterator itr = mBeings.begin();
	Beings::const_iterator itr_end = mBeings.end();

	for (; itr != itr_end; ++itr)
	{
		Being *being = (*itr);
		int xtol = being->getWidth() / 2;
		int uptol = being->getHeight();

		if ((being->mAction != Being::DEAD) && (being != player_node)
				&& (being->getPixelX() - xtol <= x) && (being->getPixelX()
				+ xtol >= x) && (being->getPixelY() - uptol <= y)
				&& (being->getPixelY() >= y))
		{
			return being;
		}
	}

	return NULL;
}

bool BeingManager::findPlayer() const
{
	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
		if (being->getName() != "vlapan" && being->getType() == Being::PLAYER)
			return true;
	}
	return false;
}

Being *BeingManager::findBeingByName(const std::string &name, Being::Type type) const
{
	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
		if (being->getName() == name && (type == Being::UNKNOWN || type
				== being->getType()))
			return being;
	}
	return NULL;
}

const Beings &BeingManager::getAll() const
{
	return mBeings;
}

void BeingManager::logic()
{
	Beings::iterator i = mBeings.begin();
	while (i != mBeings.end())
	{
		Being *being = (*i);

		being->logic();

#ifdef EATHENA_SUPPORT
		if (being->mAction == Being::DEAD && being->mFrame >= 20)
		{
			delete being;
			i = mBeings.erase(i);
		}
		else
#endif
		{
			++i;
		}
	}
}

void BeingManager::clear()
{
	if (player_node)
		mBeings.remove(player_node);

	delete_all(mBeings);
	mBeings.clear();

	if (player_node)
		mBeings.push_back(player_node);
}

Being *BeingManager::findNearestLivingBeing(int x, int y, int maxdist,
		Being::Type type) const
{
	Being *closestBeing = NULL;
	int dist = 0;

#ifdef TMWSERV_SUPPORT
	//Why do we do this:
	//For some reason x,y passed to this function is always
	//in map coords, while down below its in pixels
	//
	//I believe there is a deeper problem under this, but
	//for a temp solution we'll convert to coords to pixels
	x = x * 32;
	y = y * 32;
	maxdist = maxdist * 32;
#endif

	Beings::const_iterator itr = mBeings.begin();
	Beings::const_iterator itr_end = mBeings.end();

	for (; itr != itr_end; ++itr)
	{
		Being *being = (*itr);
#ifdef TMWSERV_SUPPORT
		const Vector &pos = being->getPosition();
		int d = abs(((int) pos.x) - x) + abs(((int) pos.y) - y);
#else
		int d = abs(being->mX - x) + abs(being->mY - y);
#endif

		if ((being->getType() == type || type == Being::UNKNOWN) && (d < dist
				|| closestBeing == NULL) // it is closer
				&& being->mAction != Being::DEAD) // no dead beings
		{
			dist = d;
			closestBeing = being;
		}
	}

	return (maxdist >= dist) ? closestBeing : NULL;
}

Being *BeingManager::findNearestLivingBeing(Being *aroundBeing, int maxdist,
		Being::Type type) const
{
	Being *closestBeing = NULL;
	int dist = 0;
#ifdef TMWSERV_SUPPORT
	const Vector &apos = aroundBeing->getPosition();
	int x = apos.x;
	int y = apos.y;
	maxdist = maxdist * 32;
#else
	int x = aroundBeing->mX;
	int y = aroundBeing->mY;
#endif

	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
#ifdef TMWSERV_SUPPORT
		const Vector &pos = being->getPosition();
		int d = abs(((int) pos.x) - x) + abs(((int) pos.y) - y);
#else
		int d = abs(being->mX - x) + abs(being->mY - y);
#endif

		if ((being->getType() == type || type == Being::UNKNOWN) && (d < dist
				|| closestBeing == NULL) // it is closer
				&& being->mAction != Being::DEAD // no dead beings
				&& being != aroundBeing)
		{
			dist = d;
			closestBeing = being;
		}
	}

	return (maxdist >= dist) ? closestBeing : NULL;
}

Being *BeingManager::findNearestLivingBeingNotName(Being *aroundBeing,
		int maxdist, Being::Type type, std::string name) const
{
	Being *closestBeing = NULL;
	int dist = 0;
#ifdef TMWSERV_SUPPORT
	const Vector &apos = aroundBeing->getPosition();
	int x = apos.x;
	int y = apos.y;
	maxdist = maxdist * 32;
#else
	int x = aroundBeing->mX;
	int y = aroundBeing->mY;
#endif

	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
		if (player_node->square && (being->mX < player_node->left || being->mX
				> player_node->right || being->mY < player_node->top
				|| being->mY > player_node->bottom))
			continue;
#ifdef TMWSERV_SUPPORT
		const Vector &pos = being->getPosition();
		int d = abs(((int) pos.x) - x) + abs(((int) pos.y) - y);
#else
		int d = abs(being->mX - x) + abs(being->mY - y);
#endif

		if ((being->getType() == type || type == Being::UNKNOWN) && (d < dist
				|| closestBeing == NULL) // it is closer
				&& being->mAction != Being::DEAD // no dead beings
				&& being != aroundBeing && (name == ""
				|| being->getName().find(name, 0) == std::string::npos))
		{
			dist = d;
			closestBeing = being;
		}
	}

	return (maxdist >= dist) ? closestBeing : NULL;
}

Being *BeingManager::findIsolatedBeing(Being *aroundBeing, int maxdist,
		Being::Type type, std::string name) const
{
	Being *dangerBeing = NULL;
	unsigned int mweight = 0;
	unsigned int tweight = 0;
	unsigned int weight = 0;
	int x = aroundBeing->mX;
	int y = aroundBeing->mY;

	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		Being *being = (*i);
		if (player_node->isBotOn && player_node->square && (being->mX
				< player_node->left || being->mX > player_node->right
				|| being->mY < player_node->top || being->mY
				> player_node->bottom))
			continue;
		int dx = abs(being->mX - x);
		int dy = abs(being->mY - y);
		int d = dx > dy ? dx : dy;
		if (d > maxdist)
			continue;
		mweight = getMonsterWeight(being->getName());
		if (mweight != 0
				&& (being->getType() == type || type == Being::UNKNOWN)
				&& being->mAction != Being::DEAD && being != aroundBeing
				&& (name.empty() || being->getName() == name) && ((x
				== being->mX && y == being->mY)
				|| aroundBeing->destinationReachable(being->mX, being->mY)))
		{
			tweight = (d + 1) * mweight;
			if (dangerBeing == NULL || tweight < weight)
			{
				weight = tweight;
				dangerBeing = being;
			}
		}
	}

	return dangerBeing ? dangerBeing : NULL;
}
int BeingManager::getMonsterWeight(std::string name) const
{
	// Danger targets
	if (std::strcmp(name.data(), "Fallen") == 0)
		return 2 * 2 + 1;
	else if (std::strcmp(name.data(), "Zombie") == 0)
		return 4 * 2 + 1;
	else if (std::strcmp(name.data(), "Skeleton") == 0)
		return 8 * 3 + 1;
	else if (std::strcmp(name.data(), "Jack") == 0)
		return 16 * 4 + 1;
	else if (std::strcmp(name.data(), "Lady Skeleton") == 0)
		return 32 * 4 + 1;

	// Kill when no danger targets, can be avoided with /avoid (0 | 1)
	else if (std::strcmp(name.data(), "Poison Skull") == 0)
		return 101 * 4 + 1;
	else if (std::strcmp(name.data(), "Fire Skull") == 0)
		return 102 * 4 + 1;
	else if (std::strcmp(name.data(), "Poltergeist") == 0)
		return 200 * 4 + 1;
	else if (std::strcmp(name.data(), "Wisp") == 0)
		return 400 * 4 + 1;

	// Avoid
	else if (std::strcmp(name.data(), "Spectre") == 0)
		return 0;

	return 10000;
}
bool BeingManager::hasBeing(Being *being) const
{
	for (Beings::const_iterator i = mBeings.begin(), i_end = mBeings.end(); i
			!= i_end; ++i)
	{
		if (being == *i)
			return true;
	}

	return false;
}
