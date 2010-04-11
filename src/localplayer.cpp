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

#include "localplayer.h"

#include "configuration.h"
#include "equipment.h"
#include "flooritem.h"
#include "game.h"
#include "graphics.h"
#include "inventory.h"
#include "item.h"
#include "map.h"
#include "monster.h"
#include "particle.h"
#include "simpleanimation.h"
#include "sound.h"
#include "statuseffect.h"
#include "text.h"

#include "gui/gui.h"
#include "gui/ministatus.h"
#include "gui/palette.h"
#ifdef EATHENA_SUPPORT
#include "gui/storagewindow.h"
#endif

#include "net/inventoryhandler.h"
#include "net/net.h"
#include "net/partyhandler.h"
#include "net/playerhandler.h"
#include "net/tradehandler.h"

#ifdef TMWSERV_SUPPORT
#include "effectmanager.h"
#include "guild.h"

#include "net/tmwserv/gameserver/player.h"
#include "net/tmwserv/chatserver/guild.h"
#else
#include "net/ea/partyhandler.h"
#include "net/ea/skillhandler.h"
#endif

#include "resources/animation.h"
#include "resources/imageset.h"
#include "resources/iteminfo.h"
#include "resources/resourcemanager.h"

#include "utils/gettext.h"
#include "utils/stringutils.h"

#include <cassert>

#ifdef TMWSERV_SUPPORT
const short walkingKeyboardDelay = 100;
#endif

bool specialSit = false;
int actionDelay = 50;
int targetDelay = 60000;

LocalPlayer *player_node = NULL;

#ifdef TMWSERV_SUPPORT
LocalPlayer::LocalPlayer():
    Player(65535, 0, NULL),
    mEquipment(new Equipment),
#else
LocalPlayer::LocalPlayer(int id, int job, Map *map):
    Player(id, job, map),
    mCharId(0),
    mJobXp(0),
    mJobLevel(0),
    mXpForNextLevel(0), mJobXpForNextLevel(0),
    mMp(0), mMaxMp(0),
    mAttackRange(0),
    ATK(0), MATK(0), DEF(0), MDEF(0), HIT(0), FLEE(0),
    ATK_BONUS(0), MATK_BONUS(0), DEF_BONUS(0), MDEF_BONUS(0), FLEE_BONUS(0),
    mStatPoint(0), mSkillPoint(0),
    mStatsPointsToAttribute(0),
    mEquipment(new Equipment),
#endif
    mInStorage(false),
#ifdef EATHENA_SUPPORT
    mXp(0),
    mTargetTime(-1),
#endif
    mLastTarget(-1),
#ifdef TMWSERV_SUPPORT
    mAttributeBase(NB_CHARACTER_ATTRIBUTES, -1),
    mAttributeEffective(NB_CHARACTER_ATTRIBUTES, -1),
    mExpCurrent(CHAR_SKILL_NB, -1),
    mExpNext(CHAR_SKILL_NB, -1),
    mCharacterPoints(-1),
    mCorrectionPoints(-1),
    mLevelProgress(0),
#endif
    mLevel(1),
    mMoney(0),
    mTotalWeight(1), mMaxWeight(1),
    mHp(1), mMaxHp(1),
    mTarget(NULL), mPickUpTarget(NULL),
    mTrading(false), mGoingToTarget(false), mKeepAttacking(false),
    mLastAction(-1),
    mWalkingDir(0),
    mDestX(0), mDestY(0),
    mInventory(new Inventory(INVENTORY_SIZE)),
#ifdef TMWSERV_SUPPORT
    mLocalWalkTime(-1),
#endif
    mStorage(new Inventory(STORAGE_SIZE))
#ifdef TMWSERV_SUPPORT
    , mExpMessageTime(0)
#endif
{
	bool noskulls = false;
    bool noPickupDelay = false;
	bool autoHeal = false;
	bool square = false;

	int idleInterval = 20000;
	std::string idleMessage = "Default IDLE message";
	bool useIdle = false;

	int top = 0;
	int bottom = 0;
	int left = 0;
	int right = 0;
	int homex = 0;
	int homey = 0;
    // Variable to keep the local player from doing certain actions before a map
    // is initialized. e.g. drawing a player's name using the TextManager, since
    // it appears to be dependant upon map coordinates for updating drawing.
    mMapInitialized = false;

    mUpdateName = true;

    initTargetCursor();
}

LocalPlayer::~LocalPlayer()
{
    delete mInventory;
#ifdef EATHENA_SUPPORT
    delete mStorage;
#endif

    for (int i = Being::TC_SMALL; i < Being::NUM_TC; i++)
    {
        delete mTargetCursor[0][i];
        delete mTargetCursor[1][i];
        mTargetCursorImages[0][i]->decRef();
        mTargetCursorImages[1][i]->decRef();
    }
}

void LocalPlayer::logic()
{
    // Actions are allowed once per second
    if (get_elapsed_time(mLastAction) >= actionDelay)
        mLastAction = -1;

#ifdef TMWSERV_SUPPORT
    // Show XP messages
    if (!mExpMessages.empty())
    {
        if (mExpMessageTime == 0)
        {
            const Vector &pos = getPosition();

            particleEngine->addTextRiseFadeOutEffect(
                    mExpMessages.front(),
                    (int) pos.x + 16,
                    (int) pos.y - 16,
                    &guiPalette->getColor(Palette::EXP_INFO),
                    gui->getInfoParticleFont(), true);

            mExpMessages.pop_front();
            mExpMessageTime = 30;
        }
        mExpMessageTime--;
    }
#else
    // Targeting allowed 4 times a second
    if (get_elapsed_time(mLastTarget) >= 250)
        mLastTarget = -1;

    // Remove target if its been on a being for more than a minute
    if (get_elapsed_time(mTargetTime) >= targetDelay)
    {
        mTargetTime = -1;
        setTarget(NULL);
        mLastTarget = -1;
    }

    if (mTarget)
    {
        if (mTarget->getType() == Being::NPC)
        {
            // NPCs are always in range
            mTarget->setTargetAnimation(
                mTargetCursor[0][mTarget->getTargetCursorSize()]);
        }
        else
        {
            // Find whether target is in range
            const int rangeX = abs(mTarget->mX - mX);
            const int rangeY = abs(mTarget->mY - mY);
            const int attackRange = getAttackRange();
            const int inRange = rangeX > attackRange || rangeY > attackRange
                                                                    ? 1 : 0;

            mTarget->setTargetAnimation(
                mTargetCursor[inRange][mTarget->getTargetCursorSize()]);

            if (mTarget->mAction == DEAD)
                stopAttack();

            if (mKeepAttacking && mTarget)
                attack(mTarget, true);
        }
    }
#endif

    Player::logic();
}

void LocalPlayer::setAction(Action action, int attackType)
{
    if (action == DEAD)
    {
        mLastTarget = -1;
        setTarget(NULL);
    }

    Player::setAction(action, attackType);
}

void LocalPlayer::setAction2(Action action)
{
    //setAction(action);
    Net::getPlayerHandler()->respawn();
}

void LocalPlayer::setGM(bool gm)
{
    mIsGM = gm;
}

void LocalPlayer::setGMLevel(int level)
{
    mGMLevel = level;

    if (level > 0)
        setGM(true);
}

void LocalPlayer::setName(const std::string &name)
{
    if (mName)
    {
        delete mName;
        mName = 0;
    }

    if (config.getValue("showownname", false) && mMapInitialized)
        Player::setName(name);
    else
        Being::setName(name);
}

void LocalPlayer::nextStep()
{
    // TODO: Fix picking up when reaching target (this method is obsolete)
    // TODO: Fix holding walking button to keep walking smoothly
    if (mPath.empty())
    {
        if (mPickUpTarget)
            pickUp(mPickUpTarget);

        if (mWalkingDir)
            walk(mWalkingDir);
    }

    // TODO: Fix automatically walking within range of target, when wanted
    if (mGoingToTarget && mTarget && withinAttackRange(mTarget))
    {
        mAction = Being::STAND;
#ifdef EATHENA_SUPPORT
        attack(mTarget, true);
#endif
        mGoingToTarget = false;
        mPath.clear();
        return;
    }
    else if (mGoingToTarget && !mTarget)
    {
        mGoingToTarget = false;
        mPath.clear();
    }

#ifdef EATHENA_SUPPORT
    Player::nextStep();
#endif
}

#ifdef TMWSERV_SUPPORT
bool LocalPlayer::checkInviteRights(const std::string &guildName)
{
    Guild *guild = getGuild(guildName);
    if (guild)
    {
        return guild->getInviteRights();
    }

    return false;
}

void LocalPlayer::inviteToGuild(Being *being)
{
    // TODO: Allow user to choose which guild to invite being to
    // For now, just invite to the first guild you have permissions to invite with
    std::map<int, Guild*>::iterator itr = mGuilds.begin();
    std::map<int, Guild*>::iterator itr_end = mGuilds.end();
    for (; itr != itr_end; ++itr)
    {
        if (checkInviteRights(itr->second->getName()))
        {
            Net::ChatServer::Guild::invitePlayer(being->getName(), itr->second->getId());
            return;
        }
    }
}

void LocalPlayer::clearInventory()
{
    mEquipment->clear();
    mInventory->clear();
}

void LocalPlayer::setInvItem(int index, int id, int amount)
{
    mInventory->setItem(index, id, amount);
}

#endif

void LocalPlayer::pickUp(FloorItem *item)
{
#ifdef TMWSERV_SUPPORT
    int dx = item->getX() - (int) getPosition().x / 32;
    int dy = item->getY() - (int) getPosition().y / 32;
#else
    int dx = item->getX() - mX;
    int dy = item->getY() - mY;
#endif

    if (dx * dx + dy * dy < 4)
    {
        Net::getPlayerHandler()->pickUp(item);
        mPickUpTarget = NULL;
    }
    else
    {
#ifdef TMWSERV_SUPPORT
        setDestination(item->getX() * 32 + 16, item->getY() * 32 + 16);
#else
        setDestination(item->getX(), item->getY());
#endif
        mPickUpTarget = item;
#ifdef EATHENA_SUPPORT
        stopAttack();
#endif
    }
}

void LocalPlayer::walk(unsigned char dir)
{
    // TODO: Evaluate the implementation of this method for tmwserv
    if (!mMap || !dir)
        return;

#ifdef TMWSERV_SUPPORT
    const Vector &pos = getPosition();
#endif

    if (mAction == WALK && !mPath.empty())
    {
        // Just finish the current action, otherwise we get out of sync
#ifdef TMWSERV_SUPPORT
        Being::setDestination(pos.x, pos.y);
#else
        Being::setDestination(mX, mY);
#endif
        return;
    }

    int dx = 0, dy = 0;
#ifdef TMWSERV_SUPPORT
    if (dir & UP)
        dy -= 32;
    if (dir & DOWN)
        dy += 32;
    if (dir & LEFT)
        dx -= 32;
    if (dir & RIGHT)
        dx += 32;
#else
    if (dir & UP)
        dy--;
    if (dir & DOWN)
        dy++;
    if (dir & LEFT)
        dx--;
    if (dir & RIGHT)
        dx++;
#endif

    // Prevent skipping corners over colliding tiles
#ifdef TMWSERV_SUPPORT
    if (dx && !mMap->getWalk(((int) pos.x + dx) / 32,
                             (int) pos.y / 32, getWalkMask()))
        dx = 16 - (int) pos.x % 32;
    if (dy && !mMap->getWalk((int) pos.x / 32,
                             ((int) pos.y + dy) / 32, getWalkMask()))
        dy = 16 - (int) pos.y % 32;
#else
    if (dx && !mMap->getWalk(mX + dx, mY, getWalkMask()))
        dx = 0;
    if (dy && !mMap->getWalk(mX, mY + dy, getWalkMask()))
        dy = 0;
#endif

    // Choose a straight direction when diagonal target is blocked
#ifdef TMWSERV_SUPPORT
    if (dx && dy && !mMap->getWalk((pos.x + dx) / 32,
                                   (pos.y + dy) / 32, getWalkMask()))
        dx = 16 - (int) pos.x % 32;

    int dScaler; // Distance to walk

    // Checks our path up to 5 tiles, if a blocking tile is found
    // We go to the last good tile, and break out of the loop
    for (dScaler = 1; dScaler <= 10; dScaler++)
    {
        if ( (dx || dy) &&
             !mMap->getWalk( ((int) pos.x + (dx * dScaler)) / 32,
                             ((int) pos.y + (dy * dScaler)) / 32, getWalkMask()) )
        {
            dScaler--;
            break;
        }
    }

    if (dScaler >= 0)
    {
         setDestination((int) pos.x + (dx * dScaler), (int) pos.y + (dy * dScaler));
    }
#else
    if (dx && dy && !mMap->getWalk(mX + dx, mY + dy, getWalkMask()))
        dx = 0;

    // Walk to where the player can actually go
    if ((dx || dy) && mMap->getWalk(mX + dx, mY + dy, getWalkMask()))
    {
        if(mAction != SIT || !specialSit)
            setDestination(mX + dx, mY + dy);
    }
#endif

    if (dir)
    {
        // If the being can't move, just change direction
        Net::getPlayerHandler()->setDirection(dir);
        setDirection(dir);
    }
}

Being *LocalPlayer::getTarget() const
{
    return mTarget;
}

void LocalPlayer::setTarget(Being *target)
{
#ifdef EATHENA_SUPPORT
    if (mLastTarget != -1 || target == this)
        return;

    mLastTarget = tick_time;

    if (target == mTarget)
        return;

    if (target || mAction == ATTACK)
    {
        mTargetTime = tick_time;
    }
    else
    {
        mKeepAttacking = false;
        mTargetTime = -1;
    }
#endif

    if (mTarget)
        mTarget->untarget();

    if (mTarget && mTarget->getType() == Being::MONSTER)
        static_cast<Monster *>(mTarget)->setShowName(false);

    mTarget = target;

    if (target && target->getType() == Being::MONSTER)
        static_cast<Monster *>(target)->setShowName(true);
}

#ifdef TMWSERV_SUPPORT
void LocalPlayer::setDestination(int x, int y)
#else
void LocalPlayer::setDestination(Uint16 x, Uint16 y)
#endif
{
#ifdef TMWSERV_SUPPORT
    // Fix coordinates so that the player does not seem to dig into walls.
    const int tx = x / 32;
    const int ty = y / 32;
    int fx = x % 32;
    int fy = y % 32;

    if (fx != 16 && !mMap->getWalk(tx + fx / 16 * 2 - 1, ty, getWalkMask()))
        fx = 16;
    if (fy != 16 && !mMap->getWalk(tx, ty + fy / 16 * 2 - 1, getWalkMask()))
        fy = 16;
    if (fx != 16 && fy != 16 && !mMap->getWalk(tx + fx / 16 * 2 - 1,
                                               ty + fy / 16 * 2 - 1,
                                               getWalkMask()))
        fx = 16;

    x = tx * 32 + fx;
    y = ty * 32 + fy;
#endif

    // Only send a new message to the server when destination changes
    if (x != mDestX || y != mDestY)
    {
        mDestX = x;
        mDestY = y;

        Net::getPlayerHandler()->setDestination(x, y, mDirection);
    }

    mPickUpTarget = NULL;
    mKeepAttacking = false;

    Being::setDestination(x, y);
}

void LocalPlayer::setWalkingDir(int dir)
{
    mWalkingDir = dir;

    // If we're not already walking, start walking.
    if (mAction != WALK && dir
#ifdef TMWSERV_SUPPORT
        && get_elapsed_time(mLocalWalkTime) >= walkingKeyboardDelay
#endif
       )
    {
        walk(dir);
    }
}

#ifdef TMWSERV_SUPPORT
void LocalPlayer::stopWalking(bool sendToServer)
{
    if (mAction == WALK && mWalkingDir) {
        mWalkingDir = 0;
        mLocalWalkTime = 0;
        Being::setDestination(getPosition().x,getPosition().y);
        if (sendToServer)
             Net::GameServer::Player::walk(getPosition().x, getPosition().y);
        setAction(STAND);
    }

    clearPath();
}
#endif

void LocalPlayer::toggleSit(bool multidirection)
{
    if (mLastAction != -1)
        return;

    mLastAction = tick_time;
    specialSit = multidirection;

    Being::Action newAction;
    switch (mAction)
    {
        case STAND: newAction = SIT; break;
        case SIT: newAction = STAND; break;
        default: return;
    }

    Net::getPlayerHandler()->changeAction(newAction);
}

void LocalPlayer::setActionDelay(int value)
{
    actionDelay = value;
}

void LocalPlayer::setTargetDelay(int value)
{
    targetDelay = value;
}

void LocalPlayer::emote(Uint8 emotion)
{
    if (mLastAction != -1)
        return;
    mLastAction = tick_time;

    Net::getPlayerHandler()->emote(emotion);
}

#ifdef TMWSERV_SUPPORT

void LocalPlayer::attack()
{
    if (mLastAction != -1)
        return;

    // Can only attack when standing still
    if (mAction != STAND && mAction != ATTACK)
        return;

    //Face direction of the target
    if(mTarget){
        unsigned char dir = 0;
        int x = 0, y = 0;
        Vector plaPos = this->getPosition();
        Vector tarPos = mTarget->getPosition();
        x = plaPos.x - tarPos.x;
        y = plaPos.y - tarPos.y;
        if(abs(x) < abs(y)){
            //Check to see if target is above me or below me
            if(y > 0){
               dir = UP;
            } else {
               dir = DOWN;
            }
        } else {
            //check to see if the target is to the left or right of me
            if(x > 0){
               dir = LEFT;
            } else {
               dir = RIGHT;
            }
        }
        setDirection(dir);
    }

    mLastAction = tick_time;

    setAction(ATTACK);

    if (mEquippedWeapon)
    {
        std::string soundFile = mEquippedWeapon->getSound(EQUIP_EVENT_STRIKE);
        if (soundFile != "") sound.playSfx(soundFile);
    }
    else {
        sound.playSfx("sfx/fist-swish.ogg");
    }
    Net::GameServer::Player::attack(getSpriteDirection());
}

void LocalPlayer::useSpecial(int special)
{
    Net::GameServer::Player::useSpecial(special);
}

#else

void LocalPlayer::attack(Being *target, bool keep)
{
    mKeepAttacking = keep;

    if (!target	|| target->getType() == Being::NPC)
        return;

    if (mTarget != target || !mTarget)
    {
        mLastTarget = -1;
        setTarget(target);
    }

    int dist_x = target->mX - mX;
    int dist_y = target->mY - mY;

    // Must be standing to attack
    if (mAction != STAND)
        return;

    if (abs(dist_y) >= abs(dist_x))
    {
        if (dist_y > 0)
            setDirection(DOWN);
        else
            setDirection(UP);
    }
    else
    {
        if (dist_x > 0)
            setDirection(RIGHT);
        else
            setDirection(LEFT);
    }

    mWalkTime = tick_time;
    mTargetTime = tick_time;

    setAction(ATTACK);

    Net::getPlayerHandler()->attack(target);

	if (!keep)
        stopAttack();
}

#endif // no TMWSERV_SUPPORT

void LocalPlayer::stopAttack()
{
    if (mTarget)
    {
        if (mAction == ATTACK)
            setAction(STAND);
        setTarget(NULL);
    }
    mLastTarget = -1;
}

#ifdef TMWSERV_SUPPORT

void LocalPlayer::raiseAttribute(size_t attr)
{
    // we assume that the server allows the change. When not we will undo it later.
    mCharacterPoints--;
    mAttributeBase.at(attr)++;
    Net::GameServer::Player::raiseAttribute(attr + CHAR_ATTR_BEGIN);
}

void LocalPlayer::lowerAttribute(size_t attr)
{
    // we assume that the server allows the change. When not we will undo it later.
    mCorrectionPoints--;
    mCharacterPoints++;
    mAttributeBase.at(attr)--;
    Net::GameServer::Player::lowerAttribute(attr + CHAR_ATTR_BEGIN);
}

const struct LocalPlayer::SkillInfo& LocalPlayer::getSkillInfo(int skill)
{
    static const SkillInfo skills[CHAR_SKILL_NB + 1] =
    {
        { _("Unarmed"), "graphics/images/unarmed.png" },   // CHAR_SKILL_WEAPON_NONE
        { _("Knife"), "graphics/images/knife.png" },       // CHAR_SKILL_WEAPON_KNIFE
        { _("Sword"), "graphics/images/sword.png" },       // CHAR_SKILL_WEAPON_SWORD
        { _("Polearm"), "graphics/images/polearm.png" },   // CHAR_SKILL_WEAPON_POLEARM
        { _("Staff"), "graphics/images/staff.png" },       // CHAR_SKILL_WEAPON_STAFF
        { _("Whip"), "graphics/images/whip.png" },         // CHAR_SKILL_WEAPON_WHIP
        { _("Bow"), "graphics/images/bow.png" },           // CHAR_SKILL_WEAPON_BOW
        { _("Shooting"), "graphics/images/shooting.png" }, // CHAR_SKILL_WEAPON_SHOOTING
        { _("Mace"), "graphics/images/mace.png" },         // CHAR_SKILL_WEAPON_MACE
        { _("Axe"), "graphics/images/axe.png" },           // CHAR_SKILL_WEAPON_AXE
        { _("Thrown"), "graphics/images/thrown.png" },     // CHAR_SKILL_WEAPON_THROWN
        { _("Magic"), "graphics/images/magic.png" },       // CHAR_SKILL_MAGIC_IAMJUSTAPLACEHOLDER
        { _("Craft"), "graphics/images/craft.png" },       // CHAR_SKILL_CRAFT_IAMJUSTAPLACEHOLDER
        { _("Unknown Skill"), "graphics/images/unknown.png" }
    };

    if ((skill < 0) || (skill > CHAR_SKILL_NB))
    {
        return skills[CHAR_SKILL_NB];
    }
    else
    {
        return skills[skill];
    }
}

void LocalPlayer::setExperience(int skill, int current, int next)
{
    int diff = current - mExpCurrent.at(skill);
    if (mMap && mExpCurrent.at(skill) != -1 && diff > 0)
    {
        const std::string text = toString(diff) + " " + getSkillInfo(skill).name + " xp";
        mExpMessages.push_back(text);
    }

    mExpCurrent.at(skill) = current;
    mExpNext.at(skill) = next;
}

std::pair<int, int> LocalPlayer::getExperience(int skill)
{
    return std::pair<int, int> (mExpCurrent.at(skill), mExpNext.at(skill));
}

#else

void LocalPlayer::setXp(int xp)
{
    if (mMap && xp > mXp)
    {
        const std::string text = toString(xp - mXp) + " xp";

        // Show XP number
        particleEngine->addTextRiseFadeOutEffect(
                text,
                getPixelX(),
                getPixelY() - 48,
                &guiPalette->getColor(Palette::EXP_INFO),
                gui->getInfoParticleFont(), true);
    }
    mXp = xp;
}

#endif

void LocalPlayer::pickedUp(const std::string &item)
{
    if (mMap)
    {
        // Show pickup notification
        particleEngine->addTextRiseFadeOutEffect(
                item,
                getPixelX(),
                getPixelY() - 48,
                &guiPalette->getColor(Palette::PICKUP_INFO),
                gui->getInfoParticleFont(), true);
    }
}

int LocalPlayer::getAttackRange()
{
#ifdef TMWSERV_SUPPORT
    Item *weapon = mEquipment->getEquipment(EQUIP_FIGHT1_SLOT);
    if (weapon)
    {
        const ItemInfo info = weapon->getInfo();
        return info.getAttackRange();
    }
    return 32; // unarmed range
#else
    return mAttackRange;
#endif
}

bool LocalPlayer::withinAttackRange(Being *target)
{
#ifdef TMWSERV_SUPPORT
    const Vector &targetPos = target->getPosition();
    const Vector &pos = getPosition();
    const int dx = abs(targetPos.x - pos.x);
    const int dy = abs(targetPos.y - pos.y);
    const int range = getAttackRange();

    return !(dx > range || dy > range);
#else
    int dist_x = abs(target->mX - mX);
    int dist_y = abs(target->mY - mY);

    if (dist_x > getAttackRange() || dist_y > getAttackRange())
    {
        return false;
    }

    return true;
#endif
}

void LocalPlayer::setGotoTarget(Being *target)
{
    mLastTarget = -1;
#ifdef TMWSERV_SUPPORT
    mTarget = target;
    mGoingToTarget = true;
    const Vector &targetPos = target->getPosition();
    setDestination(targetPos.x, targetPos.y);
#else
    setTarget(target);
    mGoingToTarget = true;
    setDestination(target->mX, target->mY);
#endif
}

extern MiniStatusWindow *miniStatusWindow;

void LocalPlayer::handleStatusEffect(StatusEffect *effect, int effectId)
{
    Being::handleStatusEffect(effect, effectId);

    if (effect)
    {
        effect->deliverMessage();
        effect->playSFX();

        AnimatedSprite *sprite = effect->getIcon();

        if (!sprite) {
            // delete sprite, if necessary
            for (unsigned int i = 0; i < mStatusEffectIcons.size();)
                if (mStatusEffectIcons[i] == effectId) {
                    mStatusEffectIcons.erase(mStatusEffectIcons.begin() + i);
                    miniStatusWindow->eraseIcon(i);
                } else i++;
        } else {
            // replace sprite or append
            bool found = false;

            for (unsigned int i = 0; i < mStatusEffectIcons.size(); i++)
                if (mStatusEffectIcons[i] == effectId) {
                    miniStatusWindow->setIcon(i, sprite);
                    found = true;
                    break;
                }

            if (!found) { // add new
                int offset = mStatusEffectIcons.size();
                miniStatusWindow->setIcon(offset, sprite);
                mStatusEffectIcons.push_back(effectId);
            }
        }
    }
}

void LocalPlayer::initTargetCursor()
{
    // Load target cursors
    loadTargetCursor("graphics/gui/target-cursor-blue-s.png", 44, 35,
                     false, TC_SMALL);
    loadTargetCursor("graphics/gui/target-cursor-red-s.png", 44, 35,
                     true, TC_SMALL);
    loadTargetCursor("graphics/gui/target-cursor-blue-m.png", 62, 44,
                     false, TC_MEDIUM);
    loadTargetCursor("graphics/gui/target-cursor-red-m.png", 62, 44,
                     true, TC_MEDIUM);
    loadTargetCursor("graphics/gui/target-cursor-blue-l.png", 82, 60,
                     false, TC_LARGE);
    loadTargetCursor("graphics/gui/target-cursor-red-l.png", 82, 60,
                     true, TC_LARGE);
}

void LocalPlayer::loadTargetCursor(const std::string &filename,
                                   int width, int height,
                                   bool outRange, TargetCursorSize size)
{
    assert(size > -1);
    assert(size < 3);

    ResourceManager *resman = ResourceManager::getInstance();

    ImageSet *currentImageSet = resman->getImageSet(filename, width, height);
    Animation *anim = new Animation;

    for (unsigned int i = 0; i < currentImageSet->size(); ++i)
    {
        anim->addFrame(currentImageSet->get(i), 75,
                      (16 - (currentImageSet->getWidth() / 2)),
                      (16 - (currentImageSet->getHeight() / 2)));
    }

    SimpleAnimation *currentCursor = new SimpleAnimation(anim);

    const int index = outRange ? 1 : 0;

    mTargetCursorImages[index][size] = currentImageSet;
    mTargetCursor[index][size] = currentCursor;
}

#ifdef EATHENA_SUPPORT
void LocalPlayer::setInStorage(bool inStorage)
{
    mInStorage = inStorage;

    storageWindow->setVisible(inStorage);
}
#endif
