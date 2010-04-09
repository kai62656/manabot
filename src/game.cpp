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
#include "channelmanager.h"
#include "commandhandler.h"
#include "configuration.h"
#include "effectmanager.h"
#include "emoteshortcut.h"
#include "engine.h"
#include "flooritemmanager.h"
#include "flooritem.h"
#include "game.h"
#include "graphics.h"
#include "itemshortcut.h"
#include "joystick.h"
#include "keyboardconfig.h"
#include "localplayer.h"
#include "log.h"
#include "main.h"
#include "map.h"
#include "npc.h"
#include "particle.h"
#include "playerrelations.h"
#include "sound.h"

#include "gui/widgets/chattab.h"
#include "gui/buy.h"
#include "gui/buysell.h"
#include "gui/chat.h"
#include "gui/confirmdialog.h"
#include "gui/debugwindow.h"
#include "gui/emoteshortcutcontainer.h"
#include "gui/equipmentwindow.h"
#include "gui/gui.h"
#include "gui/help.h"
#include "gui/inventorywindow.h"
#include "gui/shortcutwindow.h"
#include "gui/itemshortcutcontainer.h"
#include "gui/minimap.h"
#include "gui/ministatus.h"
#include "gui/npcdialog.h"
#include "gui/okdialog.h"
#include "gui/sdlinput.h"
#include "gui/sell.h"
#include "gui/setup.h"
#include "gui/skill.h"
#include "gui/status.h"
#include "gui/trade.h"
#include "gui/viewport.h"
#include "gui/windowmenu.h"
#include "gui/partywindow.h"
#ifdef TMWSERV_SUPPORT
#include "gui/buddywindow.h"
#include "gui/guildwindow.h"
#include "gui/magic.h"
#include "gui/quitdialog.h"
#endif
#include "gui/npcpostdialog.h"
#include "gui/storagewindow.h"

#include "net/generalhandler.h"
#include "net/maphandler.h"
#include "net/ea/playerhandler.h"
#include "net/net.h"

#include "net/tmwserv/inventoryhandler.h"
#include "net/ea/inventoryhandler.h"
#include "net/ea/adminhandler.h"

#include "resources/imagewriter.h"

#include "utils/gettext.h"

#include <guichan/exception.hpp>
#include <guichan/focushandler.hpp>

#include <fstream>
#include <physfs.h>
#include <sstream>
#include <string>

std::string map_path;

bool done = false;
volatile int tick_time;
volatile int fps = 0, frame = 0;

Engine *engine = NULL;
Joystick *joystick = NULL;

extern Window *weightNotice;
extern Window *deathNotice;
#ifdef TMWSERV_SUPPORT
QuitDialog *quitDialog = NULL;
#else
ConfirmDialog *exitConfirm = NULL;
#endif
OkDialog *disconnectedDialog = NULL;

ChatWindow *chatWindow;
StatusWindow *statusWindow;
MiniStatusWindow *miniStatusWindow;
BuyDialog *buyDialog;
SellDialog *sellDialog;
#ifdef EATHENA_SUPPORT
BuySellDialog *buySellDialog;
#endif
InventoryWindow *inventoryWindow;
SkillDialog *skillDialog;
PartyWindow *partyWindow;
#ifdef TMWSERV_SUPPORT
BuddyWindow *buddyWindow;
GuildWindow *guildWindow;
MagicDialog *magicDialog;
#endif
NpcDialog *npcDialog;
NpcPostDialog *npcPostDialog;
StorageWindow *storageWindow;
Minimap *minimap;
EquipmentWindow *equipmentWindow;
TradeWindow *tradeWindow;
HelpWindow *helpWindow;
DebugWindow *debugWindow;
ShortcutWindow *itemShortcutWindow;
ShortcutWindow *emoteShortcutWindow;

BeingManager *beingManager = NULL;
FloorItemManager *floorItemManager = NULL;
ChannelManager *channelManager = NULL;
CommandHandler *commandHandler = NULL;
Particle *particleEngine = NULL;
EffectManager *effectManager = NULL;

ChatTab *localChatTab = NULL;

const int MAX_TIME = 10000;

bool bAutoPickup = false;
int pickupTimer = 0;

bool isBotOn = false;
bool isBotStandby = false;
int standbyTimer = 0;
std::string targetType = "";
std::string botMaster = "Haunted";
std::string botFriend = "";
int targetTimer = 0;

bool botListen = false;

std::string mapName = "";
int wrongMapTimer = 0;
bool wrongActStarted = false;
int conversationState = 0;
int randomTimer = 0;

bool bFollowPlayer = false;
Being *targetPlayer;
bool lostTarget = false;
int tilesPassed = 0;
std::string targetName = "";
int idleTimer = 0;
int walkTimer = 0;
int stayTimer = 0;
int sysTimer = 0;
int attackTimer = 0;
int lastHealTimer = 0;
bool attackDistBool = true;
int mmX = 0;
int mmY = 0;
int gmTimer = 0;

bool manaLeach = false;
int leachTimer = 0;

bool spellTrain = false;
int spellTimer = 0;

bool randMove = false;
int randMoveTimer = 0;

/**
 * Listener used for exiting handling.
 */
namespace
{
struct ExitListener: public gcn::ActionListener
{
	void action(const gcn::ActionEvent &event)
	{
		if (event.getId() == "yes" || event.getId() == "ok")
			done = true;

#ifdef EATHENA_SUPPORT
		exitConfirm = NULL;
#endif
		disconnectedDialog = NULL;
	}
} exitListener;
}

/**
 * Advances game logic counter.
 */
Uint32 nextTick(Uint32 interval, void *param)
{
	tick_time++;
	if (tick_time == MAX_TIME)
		tick_time = 0;
	return interval;
}

/**
 * Updates fps.
 */
Uint32 nextSecond(Uint32 interval, void *param)
{
	fps = frame;
	frame = 0;

	return interval;
}

int get_elapsed_time(int start_time)
{
	if (start_time <= tick_time)
		return (tick_time - start_time) * 10;
	else
		return (tick_time + (MAX_TIME - start_time)) * 10;
}

/**
 * Create all the various globally accessible gui windows
 */
static void createGuiWindows()
{
	// Create dialogs
	chatWindow = new ChatWindow;
	buyDialog = new BuyDialog;
	sellDialog = new SellDialog;
	tradeWindow = new TradeWindow;
	partyWindow = new PartyWindow;
#ifdef TMWSERV_SUPPORT
	magicDialog = new MagicDialog;
	equipmentWindow = new EquipmentWindow(player_node->mEquipment.get());
	buddyWindow = new BuddyWindow;
	guildWindow = new GuildWindow;
#else
	buySellDialog = new BuySellDialog;
	equipmentWindow = new EquipmentWindow;
#endif
	npcDialog = new NpcDialog;
	npcPostDialog = new NpcPostDialog;
	storageWindow = new StorageWindow;
	statusWindow = new StatusWindow(player_node);
	miniStatusWindow = new MiniStatusWindow;
	inventoryWindow = new InventoryWindow;
	skillDialog = new SkillDialog;
	minimap = new Minimap;
	helpWindow = new HelpWindow;
	debugWindow = new DebugWindow;
	itemShortcutWindow = new ShortcutWindow("ItemShortcut",
			new ItemShortcutContainer);
	emoteShortcutWindow = new ShortcutWindow("EmoteShortcut",
			new EmoteShortcutContainer);

	localChatTab = new ChatTab(_("General"));

	if (config.getValue("logToChat", 0))
	{
		logger->setChatWindow(chatWindow);
	}

	Net::getGeneralHandler()->guiWindowsLoaded();
}

/**
 * Destroy all the globally accessible gui windows
 */
static void destroyGuiWindows()
{
	Net::getGeneralHandler()->guiWindowsUnloaded();
	logger->setChatWindow(NULL);
	delete localChatTab; // Need to do this first, so it can remove itself
	delete chatWindow;
	delete statusWindow;
	delete miniStatusWindow;
	delete buyDialog;
	delete sellDialog;
#ifdef EATHENA_SUPPORT
	delete buySellDialog;
#endif
	delete inventoryWindow;
	delete partyWindow;
	delete npcDialog;
	delete npcPostDialog;
#ifdef TMWSERV_SUPPORT
	delete magicDialog;
	delete buddyWindow;
	delete guildWindow;
#endif
	delete skillDialog;
	delete minimap;
	delete equipmentWindow;
	delete tradeWindow;
	delete helpWindow;
	delete debugWindow;
	delete itemShortcutWindow;
	delete emoteShortcutWindow;
	delete storageWindow;
}

Game::Game() :
	mLastTarget(Being::UNKNOWN), mLogicCounterId(0), mSecondsCounterId(0)
{
	done = false;

	createGuiWindows();

	mWindowMenu = new WindowMenu;
	windowContainer->add(mWindowMenu);

	engine = new Engine;

	beingManager = new BeingManager;
	commandHandler = new CommandHandler;
	floorItemManager = new FloorItemManager;
	channelManager = new ChannelManager;
	effectManager = new EffectManager;

	particleEngine = new Particle(NULL);
	particleEngine->setupEngine();

	// Initialize logic and seconds counters
	tick_time = 0;
	mLogicCounterId = SDL_AddTimer(10, nextTick, NULL);
	mSecondsCounterId = SDL_AddTimer(1000, nextSecond, NULL);

	// Initialize frame limiting
	config.addListener("fpslimit", this);
	optionChanged("fpslimit");

	// Initialize beings
	beingManager->setPlayer(player_node);

	Joystick::init();
	// TODO: The user should be able to choose which one to use
	// Open the first device
	if (Joystick::getNumberOfJoysticks() > 0)
		joystick = new Joystick(0);

#ifdef EATHENA_SUPPORT
	// fade out logon-music here too to give the desired effect of "flowing"
	// into the game.
	sound.fadeOutMusic(1000);
	map_path = map_path.substr(0, map_path.rfind("."));
	engine->changeMap(map_path);
#endif

	setupWindow->setInGame(true);

	/*
	 * To prevent the server from sending data before the client
	 * has initialized, I've modified it to wait for a "ping"
	 * from the client to complete its initialization
	 *
	 * Note: This only affects the latest eAthena version.  This
	 * packet is handled by the older version, but its response
	 * is ignored by the client
	 */
	Net::getMapHandler()->ping(tick_time);
}

Game::~Game()
{
	delete mWindowMenu;

	destroyGuiWindows();

	delete beingManager;
	delete player_node;
	delete floorItemManager;
	delete channelManager;
	delete commandHandler;
	delete joystick;
	delete particleEngine;
	delete engine;

	viewport->setMap(NULL);
	player_node = NULL;
	beingManager = NULL;
	floorItemManager = NULL;
	joystick = NULL;

	SDL_RemoveTimer(mLogicCounterId);
	SDL_RemoveTimer(mSecondsCounterId);
}

static bool saveScreenshot()
{
	static unsigned int screenshotCount = 0;

	SDL_Surface *screenshot = graphics->getScreenshot();

	// Search for an unused screenshot name
	std::stringstream filenameSuffix;
	std::stringstream filename;
	std::fstream testExists;
	bool found = false;

	do
	{
		screenshotCount++;
		filename.str("");
		filenameSuffix.str("");
		filename << PHYSFS_getUserDir();
#if (defined __USE_UNIX98 || defined __FreeBSD__)
		filenameSuffix << ".tmw/";
#elif defined __APPLE__
		filenameSuffix << "Desktop/";
#endif
		filenameSuffix << "TMW_Screenshot_" << screenshotCount << ".png";
		filename << filenameSuffix.str();
		testExists.open(filename.str().c_str(), std::ios::in);
		found = !testExists.is_open();
		testExists.close();
	} while (!found);

	const bool success = ImageWriter::writePNG(screenshot, filename.str());

	if (success)
	{
		std::stringstream chatlogentry;
		chatlogentry << _("Screenshot saved to ~/") << filenameSuffix.str();
		localChatTab->chatLog(chatlogentry.str(), BY_SERVER);
	}
	else
	{
		localChatTab->chatLog(_("Saving screenshot failed!"), BY_SERVER);
		logger->log("Error: could not save screenshot.");
	}

	SDL_FreeSurface(screenshot);

	return success;
}

void Game::optionChanged(const std::string &name)
{
	int fpsLimit = (int) config.getValue("fpslimit", 60);

	mMinFrameTime = fpsLimit ? 1000 / fpsLimit : 0;

	// Reset draw time to current time
	mDrawTime = tick_time * 10;
}

void handleSpell()
{
	spellTimer++;
	if (spellTimer >= 750)
	{
		spellTimer = 0;
		localChatTab->chatInput("#manpahil");
	}

}

void handleLeach()
{
	leachTimer++;
	if (leachTimer >= 50)
	{
		leachTimer = 0;
		if (rand() % 11 > 0)
		{
			int which = rand() % 11;
			switch (which)
			{
			case 0:
				which = 1;
				break;
			case 1:
				which = 2;
				break;
			case 2:
				which = 3;
				break;
			case 3:
				which = 4;
				break;
			case 4:
				which = 5;
				break;
			case 5:
				which = 6;
				break;
			case 6:
				which = 7;
				break;
			case 7:
				which = 8;
				break;
			case 8:
				which = 9;
				break;
			case 9:
				which = 10;
				break;
			case 10:
				which = 11;
				break;
			case 11:
				which = 12;
				break;
			default:
				which = 1;
				break;
			}
			player_node->emote(which);
		}
	}
}
void Game::logic()
{
	// mDrawTime has a higher granularity than gameTime in order to be able to
	// work with minimum frame durations in milliseconds.
	int gameTime = tick_time;
	mDrawTime = tick_time * 10;

	while (!done)
	{
		if (Map *map = engine->getCurrentMap())
			map->update(get_elapsed_time(gameTime));

		// Handle all necessary game logic
		while (get_elapsed_time(gameTime) > 0)
		{
			handleInput();

			pickupTimer++;
			if (bAutoPickup && (player_node->noPickupDelay || pickupTimer % 20
					== 0))
			{
				handlePickup(1);
				pickupTimer = 0;
			}

			if (player_node->autoHeal)
			{
				lastHealTimer++;
				if (lastHealTimer > 1600 && player_node->getHp()
						< player_node->getMaxHp() / 2)
				{
					itemShortcut->useItem(5);
					lastHealTimer = 0;
				}
				else if (lastHealTimer > 32000)
				{
					lastHealTimer = 1600;
				}
			}
			if (isBotOn)
				handleBot();
			else if (bFollowPlayer)
				handleFollow2();

			if (manaLeach)
				handleLeach();

			if (spellTrain)
				handleSpell();

			if (randMove)
				handleRandMove();

			engine->logic();
			gameTime++;
		}

		// This is done because at some point tick_time will wrap.
		gameTime = tick_time;

		// Update the screen when application is active, delay otherwise.
		if (SDL_GetAppState() & SDL_APPACTIVE)
		{
			// Draw a frame if either frames are not limited or enough time has
			// passed since the last frame.
			if (!mMinFrameTime || get_elapsed_time(mDrawTime / 10)
					> mMinFrameTime)
			{
				frame++;
				gui->draw();
				graphics->updateScreen();
				mDrawTime += mMinFrameTime;

				// Make sure to wrap mDrawTime, since tick_time will wrap.
				if (mDrawTime > MAX_TIME * 10)
					mDrawTime -= MAX_TIME * 10;
			}
			else
			{
				SDL_Delay(10);
			}
		}
		else
		{
			SDL_Delay(10);
			mDrawTime = tick_time * 10;
		}

		// Handle network stuff
		Net::getGeneralHandler()->flushNetwork();
		if (!Net::getGeneralHandler()->isNetworkConnected())
		{
			if (state != STATE_ERROR)
			{
				errorMessage = _("The connection to the server was lost, "
						"the program will now quit");
			}

			if (!disconnectedDialog)
			{
				disconnectedDialog = new OkDialog(_("Network Error"),
						errorMessage);
				disconnectedDialog->addActionListener(&exitListener);
				disconnectedDialog->requestMoveToTop();
			}
		}
	}
}

void Game::HandleBotResponse(std::string message, std::string sender,
		bool whisper)
{
	//    if (message.find("!login ") != std::string::npos)
	//    {
	//       message = message.substr(7);
	//        if(message == "Laticia")
	//        {
	//            botMaster = sender;
	//            commandHandler->handleCommand("w " + botMaster + " Welcome back master.", localChatTab);
	//        }
	//    }

	if (message == "!listen" && sender == botMaster)
	{
		botListen = !botListen;

		if (botListen)
		{
			localChatTab->chatLog("Listener turned on by ." + botMaster + ".",
					BY_SERVER);
			commandHandler->handleCommand("w " + botMaster
					+ " Listener turned on.", localChatTab);
		}
		else
		{
			localChatTab->chatLog("Listener turned off by ." + botMaster + ".",
					BY_SERVER);
			commandHandler->handleCommand("w " + botMaster
					+ " Listener turned off.", localChatTab);
		}
	}
	else if (message == "!sit" && sender == botMaster)
	{
		player_node->toggleSit(false);
	}
	else if (message == "!stop" && sender == botMaster)
	{
		isBotOn = false;
		isBotStandby = false;
		targetType = "";
		mapName = "";
		wrongActStarted = false;
		wrongMapTimer = 0;
		player_node->setTargetDelay(60000);
		localChatTab->chatLog("Bot turned off by ." + botMaster + ".",
				BY_SERVER);
		commandHandler->handleCommand("w " + botMaster + " Bot turned off.",
				localChatTab);
	}
	else if (message.find("!start ") != std::string::npos && sender
			== botMaster)
	{
		message = message.substr(7);

		isBotOn = true;
		isBotStandby = false;
		targetType = message;
		mapName = map_path;
		player_node->setTargetDelay(6000);
		localChatTab->chatLog("Bot turned on by " + botMaster + " ("
				+ targetType + ").", BY_SERVER);
		commandHandler->handleCommand("w " + botMaster + " Bot turned on ("
				+ targetType + ").", localChatTab);
	}
	else if (message == "!follow" && sender == botMaster)
	{
		if (isBotOn)
		{
			commandHandler->handleCommand("w " + botMaster
					+ " Turn the bot off first.", localChatTab);
			return;
		}

		if (!bFollowPlayer)
		{
			bFollowPlayer = true;
			targetName = sender;
			localChatTab->chatLog(_("Follow player turned on by player."),
					BY_SERVER);
		}
		else
		{
			bFollowPlayer = false;
			localChatTab->chatLog(_("Follow player turned off by player."),
					BY_SERVER);
			targetName = "";
		}
	}
	else if (botListen)
	{
		if (whisper && sender == botMaster)
			localChatTab->chatInput(message);
		else
		{
			if (sender != botMaster && sender != player_node->getName())
			{
				if (message.find(" : ") != std::string::npos)
					message = message.substr(message.find(" : ") + 3);
				else
					sender = "(W)" + sender;

				commandHandler->handleCommand("w " + botMaster + " " + sender
						+ ": " + message, localChatTab);
			}
		}
	}
	else if (message.find("heal") != std::string::npos
			&& player_relations.getRelation(sender) == PlayerRelation::FRIEND)
	{
		localChatTab->chatInput("#inma " + sender);
	}
}

void RandGreetTalk()
{
	static char const
			* const randMsg[] =
			{ N_("Hello."), N_("Hi, how are you?."), N_("Hey there."),
					N_("Salut."), N_("Hey, what's up?."), N_("Bonjour"),
					N_("Greetings friend.") };
	const int random1 = rand() % (sizeof(randMsg) / sizeof(randMsg[0]));
	localChatTab->chatInput(gettext(randMsg[random1]));
}

void RandByeTalk()
{
	static char const * const randMsg[] =
	{ N_("Goodbye."), N_("So long."), N_("Adios."), N_("Take care."),
			N_("See you later."), N_("Take it easy."), N_("Farewell.") };
	const int random1 = rand() % (sizeof(randMsg) / sizeof(randMsg[0]));
	localChatTab->chatInput(gettext(randMsg[random1]));
}
void RandThanksTalk()
{
	static char const * const randMsg[] =
	{ N_("Thanks."), N_("Thank you."), N_("Cheers."), N_("Much obliged."),
			N_("Gracias."), N_("Merci.") };
	const int random1 = rand() % (sizeof(randMsg) / sizeof(randMsg[0]));
	localChatTab->chatInput(gettext(randMsg[random1]));
}
void RandMkTalk()
{
	static char const * const randMsg[] =
	{ N_("Excellent."), N_("Superb."), N_("FINISH HIM!") };
	const int random1 = rand() % (sizeof(randMsg) / sizeof(randMsg[0]));
	localChatTab->chatInput(gettext(randMsg[random1]));
}
void RandTauntsTalk()
{
	static char const
			* const randMsg[] =
			{
					N_("Your teeth will make a fine necklace."),
					N_("Victory shall be mine!"),
					N_("Save yourself the pain and kill yourself."),
					N_("Soon you will be maggot food!"),
					N_("I will paint the ground red with your blood."),
					N_("Your inferiority disgusts me. You are no challenge."),
					N_("I have seen silkworms more dangerous than you!."),
					N_("Not even Elanore herself will beable to heal you the time I am done."),
					N_("I do enjoy a challenge so killing you will be no fun."),
					N_("You are no match for my powers! I am a God!"),
					N_("I am the storm! All shall fall before me!"),
					N_("I am the bringer of death and destruction. You stand no chance."),
					N_("Bwhahaha! Now you shall feel my wrath!"),
					N_("You are pathetic and weak, this will be easy!"),
					N_("Now you shall die!"),
					N_("I will annihilate your flesh and feast on your soul!"),
					N_("I predict death in your near future."),
					N_("Your brains would look good splattered on the ground."),
					N_("You disgusting weakling, I shall crush you!"),
					N_("My killing power is supreme! You have nothing left to do but die!"),
					N_("Take notes. This is how a professional does it."),
					N_("I am the perfect killing machine."),
					N_("There will be nothing left of you to bury.") };
	const int random1 = rand() % (sizeof(randMsg) / sizeof(randMsg[0]));
	localChatTab->chatInput(gettext(randMsg[random1]));
}

void Game::handleRandMove()
{
	randMoveTimer++;
	if (randMoveTimer >= 5)
	{
		randMoveTimer = 0;
		takeRandomStep(2);
	}

}

void Game::handleBot()
{
	if (map_path != mapName)
	{
		//WrongMapActing();
		isBotOn = false;
		//		player_node->setDestination(player_node->mX, player_node->mY+1);
		return;
	}

	if (!player_node->getTarget())
	{
		stayTimer++;
		//				if(stayTimer > 6000){
		//					stayTimer = 0;
		//				}
		if (stayTimer > 100)
		{
			if (player_node->mAction != Being::SIT)
				player_node->toggleSit(false);
			//				}else{
			//		  		takeRandomStep(2);
			stayTimer = 0;
		}

		//        targetTimer++;
		//        if(handlePickup(18)){
		//            return;
		//		}
		//        if(targetTimer > 10){
		//            if (player_node->mAction != Being::ATTACK){
		//              player_node->setTarget(beingManager->findIsolatedBeing(player_node, 18, Being::MONSTER, targetType));
		//		player_node->setAction(Being::STAND);
		if (stayTimer % 20 == 0)
			player_node->getTarget()->untarget();
		if (bAutoPickup && handlePickup(1))
			return;

		player_node->setTarget(beingManager->findNearestLivingBeingNotName(
				player_node, 20, Being::MONSTER,
				player_node->noskulls ? "Skull" : ""));
		attackTimer = 0;
		sysTimer = 0;
		stayTimer = 0;
		if (player_node->square && !player_node->getTarget())
		{
			player_node->setDestination(player_node->homex, player_node->homey);
		}
		//		}
		//		return;
		//           targetTimer = 0;
		//        }
	}
	else
	{
		sysTimer++;
		//		if(sysTimer % 50 == 0){
		//			takeRandomStep(1);
		//		}
		if (sysTimer > 1000)
		{
			sysTimer = 0;
		}
		if (player_node->mX != player_node->getTarget()->mX || player_node->mY
				!= player_node->getTarget()->mY)
		{
			attackDistBool = true;
			attackTimer++;
			if (player_node->withinAttackRange(player_node->getTarget()))
			{
				player_node->setDestination(player_node->mX, player_node->mY);
				//			  player_node->setAction(Being::STAND);
				player_node->attack(player_node->getTarget(), true);
				if (sysTimer % 50 != 0)
				{
					attackDistBool = false;
				}
			}
			else if (attackTimer % 10 == 0)
			{
				player_node->setDestination(player_node->mX, player_node->mY);
				player_node->getTarget()->untarget();
				attackTimer = 0;
				return;
			}
			if (attackDistBool && player_node->mAction != Being::WALK)
			{
				//            	player_node->setGotoTarget(player_node->getTarget());
				player_node->setDestination(player_node->getTarget()->mX,
						player_node->getTarget()->mY);
			}
		}
		else
		{
			attackTimer = 0;
			if (player_node->mAction != Being::ATTACK)
			{
				player_node->attack(player_node->getTarget(), true);
			}
		}
		targetTimer = 0;
	}
}

void Game::takeRandomStep(int distance)
{
	if (randomTimer >= 20)
	{
		if (player_node->mAction != Being::WALK)
		{
			int randmove = rand() % 4 + 1;
			if (randmove == 1)
				player_node->setDestination(player_node->mX, player_node->mY
						- distance);
			else if (randmove == 2)
				player_node->setDestination(player_node->mX, player_node->mY
						+ distance);
			else if (randmove == 3)
				player_node->setDestination(player_node->mX - distance,
						player_node->mY);
			else if (randmove == 4)
				player_node->setDestination(player_node->mX + distance,
						player_node->mY);
		}
		randomTimer = 0;
	}
	else
		randomTimer++;
}

bool Game::handlePickup(int range)
{
	Uint16 x = player_node->mX;
	Uint16 y = player_node->mY;

	for (int i = x - range; i <= x + range; i++)
	{
		for (int j = y - range; j <= y + range; j++)
		{
			if (FloorItem *item = floorItemManager->findByCoordinates(i, j))
			{
				player_node->pickUp(item);
				return true;
			}
		}
	}
	return false;
}

void Game::handleFollow2()
{
	Being *target = beingManager->findBeingByName(targetName);
	if (target)
	{
		if (player_node->mX != target->mX || player_node->mY != target->mY)
			player_node->setDestination(target->mX, target->mY);
		if (target->mAction != Being::ATTACK)
		{
			if (player_node->getTarget())
				player_node->getTarget()->untarget();
			if (player_node->mAction == Being::SIT)
				player_node->toggleSit(false);
		}
		if (target->mAction == Being::ATTACK && player_node->mAction
				!= Being::ATTACK)
		{
			if (player_node->getDirection() != target->getDirection())
			{
				Net::getPlayerHandler()->setDirection(target->getDirection());
				player_node->setDirection(target->getDirection());
			}
			Being *attackTarget = beingManager->findNearestLivingBeing(target,
					player_node->getAttackRange() + 3, Being::MONSTER);
			if (attackTarget)
				player_node->attack(attackTarget, true);
		}
	}
	else
	{
		target = beingManager->findBeingByName(targetName);
		if (!target && tilesPassed < 300)
		{
			switch (player_node->getSpriteDirection())
			{
			case DIRECTION_UP:
				player_node->setWalkingDir(Being::UP);
				break;
			case DIRECTION_DOWN:
				player_node->setWalkingDir(Being::DOWN);
				break;
			case DIRECTION_LEFT:
				player_node->setWalkingDir(Being::LEFT);
				break;
			case DIRECTION_RIGHT:
				player_node->setWalkingDir(Being::RIGHT);
				break;
			}
			tilesPassed++;
		}
		else
		{
			tilesPassed = 0;
		}
	}

	void Game::handleInput()
	{
		if (joystick)
			joystick->update();

		// Events
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			bool used = false;

			// Keyboard events (for discontinuous keys)
			if (event.type == SDL_KEYDOWN)
			{
				gcn::Window *requestedWindow = NULL;

				if (setupWindow->isVisible() && keyboard.getNewKeyIndex()
						> keyboard.KEY_NO_VALUE)
				{
					keyboard.setNewKey((int) event.key.keysym.sym);
					keyboard.callbackNewKey();
					keyboard.setNewKeyIndex(keyboard.KEY_NO_VALUE);
					return;
				}

				// send straight to gui for certain windows
				if (npcPostDialog->isVisible())
				{
					try
					{
						guiInput->pushInput(event);
					} catch (gcn::Exception e)
					{
						const char* err = e.getMessage().c_str();
						logger->log("Warning: guichan input exception: %s", err);
					}
					return;
				}

				// Mode switch to emotes
				if (keyboard.isKeyActive(keyboard.KEY_EMOTE))
				{
					// Emotions
					int emotion = keyboard.getKeyEmoteOffset(
							event.key.keysym.sym);
					if (emotion)
					{
						emoteShortcut->useEmote(emotion);
						used = true;
						return;
					}
				}

				if (!chatWindow->isInputFocused()
						&& !gui->getFocusHandler()->getModalFocused())
				{
					if (keyboard.isKeyActive(keyboard.KEY_OK))
					{
#ifdef TMWSERV_SUPPORT
						// Do not focus chat input when quit dialog is active
						if (quitDialog != NULL && quitDialog->isVisible())
						continue;
#else
						if (exitConfirm && keyboard.isKeyActive(
								keyboard.KEY_TOGGLE_CHAT))
							done = true;
#endif
						// Close the Browser if opened
						else if (helpWindow->isVisible()
								&& keyboard.isKeyActive(keyboard.KEY_OK))
							helpWindow->setVisible(false);
						// Close the config window, cancelling changes if opened
						else if (setupWindow->isVisible()
								&& keyboard.isKeyActive(keyboard.KEY_OK))
							setupWindow->action(
									gcn::ActionEvent(NULL, "cancel"));
						else if (npcDialog->isVisible()
								&& keyboard.isKeyActive(keyboard.KEY_OK))
							npcDialog->action(gcn::ActionEvent(NULL, "ok"));
						/*
						 else if (guildWindow->isVisible())
						 {
						 // TODO: Check if a dialog is open and close it if so
						 }
						 */
					}
					if (keyboard.isKeyActive(keyboard.KEY_TOGGLE_CHAT))
					{
						if (chatWindow->requestChatFocus())
							used = true;
					}
					if (npcDialog->isVisible())
					{
						if (keyboard.isKeyActive(keyboard.KEY_MOVE_UP))
							npcDialog->move(1);
						else if (keyboard.isKeyActive(keyboard.KEY_MOVE_DOWN))
							npcDialog->move(-1);
					}
				}

				if (!chatWindow->isInputFocused() || (event.key.keysym.mod
						& KMOD_ALT))
				{
					if (keyboard.isKeyActive(keyboard.KEY_PREV_CHAT_TAB))
					{
						chatWindow->prevTab();
						return;
					}
					else if (keyboard.isKeyActive(keyboard.KEY_NEXT_CHAT_TAB))
					{
						chatWindow->nextTab();
						return;
					}
				}

				const int tKey = keyboard.getKeyIndex(event.key.keysym.sym);
				switch (tKey)
				{
				case KeyboardConfig::KEY_SCROLL_CHAT_UP:
					if (chatWindow->isVisible())
					{
						chatWindow->scroll(-DEFAULT_CHAT_WINDOW_SCROLL);
						used = true;
					}
					break;
				case KeyboardConfig::KEY_SCROLL_CHAT_DOWN:
					if (chatWindow->isVisible())
					{
						chatWindow->scroll(DEFAULT_CHAT_WINDOW_SCROLL);
						used = true;
						return;
					}
					break;
				case KeyboardConfig::KEY_WINDOW_HELP:
					// In-game Help
					if (helpWindow->isVisible())
						helpWindow->setVisible(false);
					else
					{
						helpWindow->loadHelp("index");
						helpWindow->requestMoveToTop();
					}
					used = true;
					break;
					// Quitting confirmation dialog
				case KeyboardConfig::KEY_QUIT:
#ifdef TMWSERV_SUPPORT
					if (!quitDialog)
					{
						quitDialog = new QuitDialog(&done, &quitDialog);
						quitDialog->requestMoveToTop();
					}
					else
					{
						quitDialog->action(gcn::ActionEvent(NULL, "cancel"));
					}
#else
					if (!exitConfirm)
					{
						exitConfirm = new ConfirmDialog(_("Quit"),
								_("Are you sure you "
										"want to quit?"));
						exitConfirm->addActionListener(&exitListener);
						exitConfirm->requestMoveToTop();
					}
					else
					{
						exitConfirm->action(gcn::ActionEvent(NULL, _("no")));
					}
#endif
					break;
				default:
					break;
				}
				if (keyboard.isEnabled() && !chatWindow->isInputFocused()
						&& !npcDialog->isInputFocused())
				{
					const int tKey = keyboard.getKeyIndex(event.key.keysym.sym);

					// Do not activate shortcuts if tradewindow is visible
					if (!tradeWindow->isVisible())
					{
						// Checks if any item shortcut is pressed.
						for (int i = KeyboardConfig::KEY_SHORTCUT_1; i
								<= KeyboardConfig::KEY_SHORTCUT_12; i++)
						{
							if (tKey == i && !used)
							{
								itemShortcut->useItem(i
										- KeyboardConfig::KEY_SHORTCUT_1);
								break;
							}
						}
					}

					switch (tKey)
					{
					case KeyboardConfig::KEY_PICKUP:
					{
#ifdef TMWSERV_SUPPORT
						const Vector &pos = player_node->getPosition();
						Uint16 x = (int) pos.x / 32;
						Uint16 y = (int) pos.y / 32;
#else
						Uint16 x = player_node->mX;
						Uint16 y = player_node->mY;
#endif
						FloorItem *item = floorItemManager->findByCoordinates(
								x, y);

						// If none below the player, try the tile in front
						// of the player
						if (!item)
						{
							// Temporary until tile-based picking is
							// removed.
							switch (player_node->getSpriteDirection())
							{
							case DIRECTION_UP:
								--y;
								break;
							case DIRECTION_DOWN:
								++y;
								break;
							case DIRECTION_LEFT:
								--x;
								break;
							case DIRECTION_RIGHT:
								++x;
								break;
							default:
								break;
							}

							item = floorItemManager->findByCoordinates(x, y);
						}

						if (item)
							player_node->pickUp(item);

						used = true;
					}
						break;
					case KeyboardConfig::KEY_BOT:
						if (!isBotOn)
						{
							if (player_node->getTarget())
							{
								isBotOn = true;
								isBotStandby = false;
								targetType
										= player_node->getTarget()->getName();
								player_node->setTargetDelay(6000);
								mapName = map_path;
								localChatTab->chatLog(_("Bot turned on."),
										BY_SERVER);
							}
							else
								localChatTab->chatLog(
										_("Bot cannot turn on. No Target."),
										BY_SERVER);
						}
						else
						{
							isBotOn = false;
							isBotStandby = false;
							targetType = "";
							mapName = "";
							wrongActStarted = false;
							wrongMapTimer = 0;
							player_node->setTargetDelay(60000);
							localChatTab->chatLog(_("Bot turned off."),
									BY_SERVER);
						}

						used = true;
						break;
					case KeyboardConfig::KEY_HEAL:
						if (player_node->getTarget())
							localChatTab->chatInput("#inma "
									+ player_node->getTarget()->getName());
						else
							localChatTab->chatInput("#lum");
						break;
					case KeyboardConfig::KEY_ARROW:
						localChatTab->chatInput("#frillyar");
						RandTauntsTalk();
						break;
					case KeyboardConfig::KEY_LIGHTNING:
						localChatTab->chatInput("#ingrav");
						RandTauntsTalk();
						break;
					case KeyboardConfig::KEY_GREET:
						RandGreetTalk();
						break;
					case KeyboardConfig::KEY_BYE:
						RandByeTalk();
						break;
					case KeyboardConfig::KEY_TAUNTS:
						RandTauntsTalk();
						break;
					case KeyboardConfig::KEY_THANKS:
						RandThanksTalk();
						break;
					case KeyboardConfig::KEY_MK:
						RandMkTalk();
						break;
					case KeyboardConfig::KEY_RANDMOVE:
						randMove = !randMove;
						if (randMove)
						{
							randMoveTimer = 0;
							localChatTab->chatLog(_("Random movement enabled"),
									BY_SERVER);
						}
						else
						{
							randMoveTimer = 0;
							localChatTab->chatLog(
									_("Random movement disabled"),
									BY_SERVER);
						}
						break;
					case KeyboardConfig::KEY_SPAMEMOTE:
						manaLeach = !manaLeach;
						if (manaLeach)
						{
							leachTimer = 0;
							localChatTab->chatLog(_("Emote spam enabled"),
									BY_SERVER);
						}
						else
						{
							leachTimer = 0;
							localChatTab->chatLog(_("Emote spam disabled"),
									BY_SERVER);
						}
						break;
					case KeyboardConfig::KEY_LIFESTONES:
						spellTrain = !spellTrain;
						if (spellTrain)
						{
							spellTimer = 0;
							localChatTab->chatLog(
									_("Auto life stone creation enabled"),
									BY_SERVER);
						}
						else
						{
							spellTimer = 0;
							localChatTab->chatLog(
									_("Auto life stone creation disabled"),
									BY_SERVER);
						}
						break;
					case KeyboardConfig::KEY_AGGRO:
						localChatTab->chatInput("#itenplz");
						break;
					case KeyboardConfig::KEY_AUTOPICKUP:
						bAutoPickup = !bAutoPickup;

						if (bAutoPickup)
						{
							localChatTab->chatLog(_("Auto Pickup turned on."),
									BY_SERVER);
							Net::getAdminHandler()->announce("?");
						}
						else
						{
							localChatTab->chatLog(_("Auto Pickup turned off."),
									BY_SERVER);
							Net::getAdminHandler()->localAnnounce("?");
						}

						break;
					case KeyboardConfig::KEY_FOLLOW:
						if (!bFollowPlayer)
						{
							if (player_node->getTarget())
							{
								bFollowPlayer = true;
								targetName
										= player_node->getTarget()->getName();
								localChatTab->chatLog(
										_("Follow player turned on."),
										BY_SERVER);
							}
							else
								localChatTab->chatLog(
										_("Follow player failed. No Target."),
										BY_SERVER);
						}
						else
						{
							bFollowPlayer = false;
							localChatTab->chatLog(
									_("Follow player turned off."),
									BY_SERVER);
							targetName = "";
						}

						break;
					case KeyboardConfig::KEY_SIT:
						// Player sit action
						player_node->toggleSit(false);
						used = true;
						break;
					case KeyboardConfig::KEY_SIT2:
						// Player sit action
						player_node->toggleSit(true);
						used = true;
						break;
					case KeyboardConfig::KEY_HIDE_WINDOWS:
						// Hide certain windows
						if (!chatWindow->isInputFocused())
						{
							statusWindow->setVisible(false);
							inventoryWindow->setVisible(false);
							skillDialog->setVisible(false);
							setupWindow->setVisible(false);
							equipmentWindow->setVisible(false);
							helpWindow->setVisible(false);
							debugWindow->setVisible(false);
#ifdef TMWSERV_SUPPORT
							guildWindow->setVisible(false);
							buddyWindow->setVisible(false);
#endif
						}
						break;
					case KeyboardConfig::KEY_WINDOW_STATUS:
						requestedWindow = statusWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_INVENTORY:
						requestedWindow = inventoryWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_EQUIPMENT:
						requestedWindow = equipmentWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_SKILL:
						requestedWindow = skillDialog;
						break;
					case KeyboardConfig::KEY_WINDOW_MINIMAP:
						minimap->toggle();
						break;
					case KeyboardConfig::KEY_WINDOW_CHAT:
						requestedWindow = chatWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_SHORTCUT:
						requestedWindow = itemShortcutWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_SETUP:
						requestedWindow = setupWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_DEBUG:
						requestedWindow = debugWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_PARTY:
						requestedWindow = partyWindow;
						break;
					case KeyboardConfig::KEY_WINDOW_EMOTE_SHORTCUT:
						requestedWindow = emoteShortcutWindow;
						break;
					case KeyboardConfig::KEY_SCREENSHOT:
						// Screenshot (picture, hence the p)
						saveScreenshot();
						used = true;
						break;
					case KeyboardConfig::KEY_PATHFIND:
						// Find path to mouse (debug purpose)
						viewport->toggleDebugPath();
						used = true;
						break;
					case KeyboardConfig::KEY_TRADE:
						// Toggle accepting of incoming trade requests
						unsigned int deflt = player_relations.getDefault();
						if (deflt & PlayerRelation::TRADE)
						{
							localChatTab->chatLog(
									_("Ignoring incoming trade requests"),
									BY_SERVER);
							deflt &= ~PlayerRelation::TRADE;
						}
						else
						{
							localChatTab->chatLog(
									_("Accepting incoming trade requests"),
									BY_SERVER);
							deflt |= PlayerRelation::TRADE;
						}

						player_relations.setDefault(deflt);

						used = true;
						break;
					}
				}

				if (requestedWindow)
				{
					requestedWindow->setVisible(!requestedWindow->isVisible());
					if (requestedWindow->isVisible())
						requestedWindow->requestMoveToTop();
					used = true;
				}
			}
			// Quit event
			else if (event.type == SDL_QUIT)
			{
				done = true;
			}

			// Push input to GUI when not used
			if (!used)
			{
				try
				{
					guiInput->pushInput(event);
				} catch (gcn::Exception e)
				{
					const char* err = e.getMessage().c_str();
					logger->log("Warning: guichan input exception: %s", err);
				}
			}

		} // End while

		// If the user is configuring the keys then don't respond.
		if (!keyboard.isEnabled())
			return;

		// Moving player around
		if (player_node->mAction != Being::DEAD && current_npc == 0
				&& !chatWindow->isInputFocused())
		{
			// Get the state of the keyboard keys
			keyboard.refreshActiveKeys();

			// Ignore input if either "ignore" key is pressed
			// Stops the character moving about if the user's window manager
			// uses "ignore+arrow key" to switch virtual desktops.
			if (keyboard.isKeyActive(keyboard.KEY_IGNORE_INPUT_1)
					|| keyboard.isKeyActive(keyboard.KEY_IGNORE_INPUT_2))
			{
				return;
			}

#ifdef TMWSERV_SUPPORT
			const Vector &pos = player_node->getPosition();
			const Uint16 x = (int) pos.x / 32;
			const Uint16 y = (int) pos.y / 32;
#else
			const Uint16 x = player_node->mX;
			const Uint16 y = player_node->mY;
#endif
			unsigned char direction = 0;

			// Translate pressed keys to movement and direction
			if (keyboard.isKeyActive(keyboard.KEY_MOVE_UP) || (joystick
					&& joystick->isUp()))
			{
				direction |= Being::UP;
			}
			else if (keyboard.isKeyActive(keyboard.KEY_MOVE_DOWN) || (joystick
					&& joystick->isDown()))
			{
				direction |= Being::DOWN;
			}

			if (keyboard.isKeyActive(keyboard.KEY_MOVE_LEFT) || (joystick
					&& joystick->isLeft()))
			{
				direction |= Being::LEFT;
			}
			else if (keyboard.isKeyActive(keyboard.KEY_MOVE_RIGHT) || (joystick
					&& joystick->isRight()))
			{
				direction |= Being::RIGHT;
			}

#ifdef TMWSERV_SUPPORT
			// First if player is pressing key for the direction he is already
			// going
			if (direction == player_node->getWalkingDir())
			{
				player_node->setWalkingDir(direction);
			}
			// Else if he is pressing a key, and its different from what he has
			// been pressing, stop (do not send this stop to the server) and
			// start in the new direction

			else if (direction && direction != player_node->getWalkingDir())
			{
				player_node->stopWalking(false);
				player_node->setWalkingDir(direction);
			}
			// Else, he is not pressing a key, stop (sending to server)

			else
			{
				player_node->stopWalking(true);
			}
#else
			player_node->setWalkingDir(direction);

			// Attacking monsters
			if (keyboard.isKeyActive(keyboard.KEY_ATTACK) || (joystick
					&& joystick->buttonPressed(0)))
			{
				if (player_node->getTarget())
					player_node->attack(player_node->getTarget(), true);
			}

			if (keyboard.isKeyActive(keyboard.KEY_TARGET_ATTACK))
			{
				Being *target = NULL;

				bool newTarget = !keyboard.isKeyActive(keyboard.KEY_TARGET);
				// A set target has highest priority
				if (!player_node->getTarget())
				{
					Uint16 targetX = x, targetY = y;
					// Only auto target Monsters
					target = beingManager->findNearestLivingBeing(targetX,
							targetY, 20, Being::MONSTER);
				}
				player_node->attack(target, newTarget);
			}

#endif

			// Target the nearest player/monster/npc
			if ((keyboard.isKeyActive(keyboard.KEY_TARGET_PLAYER)
					|| keyboard.isKeyActive(keyboard.KEY_TARGET_CLOSEST)
					|| keyboard.isKeyActive(keyboard.KEY_TARGET_NPC)
					|| (joystick && joystick->buttonPressed(3)))
					&& !keyboard.isKeyActive(keyboard.KEY_TARGET))
			{
				Being::Type currentTarget = Being::UNKNOWN;
				if (keyboard.isKeyActive(keyboard.KEY_TARGET_CLOSEST)
						|| (joystick && joystick->buttonPressed(3)))
					currentTarget = Being::MONSTER;
				else if (keyboard.isKeyActive(keyboard.KEY_TARGET_PLAYER))
					currentTarget = Being::PLAYER;
				else if (keyboard.isKeyActive(keyboard.KEY_TARGET_NPC))
					currentTarget = Being::NPC;

				Being *target = beingManager->findNearestLivingBeing(
						player_node, 20, currentTarget);

				if (target && (target != player_node->getTarget()
						|| currentTarget != mLastTarget))
				{
					player_node->setTarget(target);
					mLastTarget = currentTarget;
				}
			}
			else
				mLastTarget = Being::UNKNOWN; // Reset last target

			// Talk to the nearest NPC if 't' pressed
			if (keyboard.isKeyActive(keyboard.KEY_TALK))
			{
				if (!npcDialog->isVisible())
				{
					Being *target = player_node->getTarget();

					if (target)
					{
						if (target->getType() == Being::NPC)
							dynamic_cast<NPC*> (target)->talk();
					}
				}
			}

			// Stop attacking if the right key is pressed
			if (!keyboard.isKeyActive(keyboard.KEY_ATTACK)
					&& keyboard.isKeyActive(keyboard.KEY_TARGET))
			{
				player_node->stopAttack();
			}

			if (joystick)
			{
				if (joystick->buttonPressed(1))
				{
					FloorItem *item = floorItemManager->findByCoordinates(x, y);

					if (item)
						player_node->pickUp(item);
				}
				else if (joystick->buttonPressed(2))
				{
					player_node->toggleSit(false);
				}
			}
		}
	}
