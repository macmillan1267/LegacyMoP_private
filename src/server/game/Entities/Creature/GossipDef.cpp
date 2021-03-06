/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "QuestDef.h"
#include "GossipDef.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Formulas.h"

GossipMenu::GossipMenu()
{
    _menuId = 0;
}

GossipMenu::~GossipMenu()
{
    ClearMenu();
}

void GossipMenu::AddMenuItem(int32 menuItemId, uint8 icon, std::string const& message, uint32 sender, uint32 action, std::string const& boxMessage, uint32 boxMoney, bool coded /*= false*/)
{
    if(_menuItems.size() > GOSSIP_MAX_MENU_ITEMS)
        return;
    //ASSERT(_menuItems.size() <= GOSSIP_MAX_MENU_ITEMS);

    // Find a free new id - script case
    if (menuItemId == -1)
    {
        menuItemId = 0;
        if (!_menuItems.empty())
        {
            for (GossipMenuItemContainer::const_iterator itr = _menuItems.begin(); itr != _menuItems.end(); ++itr)
            {
                if (int32(itr->first) > menuItemId)
                    break;

                menuItemId = itr->first + 1;
            }
        }
    }

    GossipMenuItem& menuItem = _menuItems[menuItemId];

    menuItem.MenuItemIcon    = icon;
    menuItem.Message         = message;
    menuItem.IsCoded         = coded;
    menuItem.Sender          = sender;
    menuItem.OptionType      = action;
    menuItem.BoxMessage      = boxMessage;
    menuItem.BoxMoney        = boxMoney;
}
/**
 * @name AddMenuItem
 * @brief Adds a localized gossip menu item from db by menu id and menu item id.
 * @param menuId Gossip menu id.
 * @param menuItemId Gossip menu item id.
 * @param sender Identifier of the current menu.
 * @param action Custom action given to OnGossipHello.
 */
void GossipMenu::AddMenuItem(uint32 menuId, uint32 menuItemId, uint32 sender, uint32 action)
{
    /// Find items for given menu id.
    GossipMenuItemsMapBounds bounds = sObjectMgr->GetGossipMenuItemsMapBounds(menuId);
    /// Return if there are none.
    if (bounds.first == bounds.second)
        return;

    /// Iterate over each of them.
    for (GossipMenuItemsContainer::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        /// Find the one with the given menu item id.
        if (itr->second.OptionIndex != menuItemId)
            continue;

        /// Store texts for localization.
        std::string strOptionText = itr->second.OptionText;
        std::string strBoxText = itr->second.BoxText;

        /// Check need of localization.
        if (GetLocale() > LOCALE_enUS)
            /// Find localizations from database.
            if (GossipMenuItemsLocale const* no = sObjectMgr->GetGossipMenuItemsLocale(MAKE_PAIR32(menuId, menuItemId)))
            {
                /// Translate texts if there are any.
                ObjectMgr::GetLocaleString(no->OptionText, GetLocale(), strOptionText);
                ObjectMgr::GetLocaleString(no->BoxText, GetLocale(), strBoxText);
            }

        /// Add menu item with existing method. Menu item id -1 is also used in ADD_GOSSIP_ITEM macro.
        AddMenuItem(-1, itr->second.OptionIcon, strOptionText, sender, action, strBoxText, itr->second.BoxMoney, itr->second.BoxCoded);
    }
}

void GossipMenu::AddGossipMenuItemData(uint32 menuItemId, uint32 gossipActionMenuId, uint32 gossipActionPoi)
{
    GossipMenuItemData& itemData = _menuItemData[menuItemId];

    itemData.GossipActionMenuId  = gossipActionMenuId;
    itemData.GossipActionPoi     = gossipActionPoi;
}

uint32 GossipMenu::GetMenuItemSender(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return 0;

    return itr->second.Sender;
}

uint32 GossipMenu::GetMenuItemAction(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return 0;

    return itr->second.OptionType;
}

bool GossipMenu::IsMenuItemCoded(uint32 menuItemId) const
{
    GossipMenuItemContainer::const_iterator itr = _menuItems.find(menuItemId);
    if (itr == _menuItems.end())
        return false;

    return itr->second.IsCoded;
}

void GossipMenu::ClearMenu()
{
    _menuItems.clear();
    _menuItemData.clear();
}

PlayerMenu::PlayerMenu(WorldSession* session) : _session(session)
{
}

PlayerMenu::~PlayerMenu()
{
    ClearMenus();
}

void PlayerMenu::ClearMenus()
{
    _gossipMenu.ClearMenu();
    _questMenu.ClearMenu();
}

void PlayerMenu::SendGossipMenu(uint32 titleTextId, uint64 objectGUID) const
{
    WorldPacket data(SMSG_GOSSIP_MESSAGE, 100);         // guess size

    data.WriteBits(_gossipMenu.GetMenuItemCount(), 20);
    data.WriteGuidMask<5, 1, 7, 2>(objectGUID);
    data.WriteBits(_questMenu.GetMenuItemCount(), 19);
    data.WriteGuidMask<6, 4, 0>(objectGUID);

    ByteBuffer buffer;
    for (uint32 iI = 0; iI < _questMenu.GetMenuItemCount(); ++iI)
    {
        QuestMenuItem const& item = _questMenu.GetItem(iI);
        uint32 questID = item.QuestId;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questID);

        data.WriteBit(0);                               // 3.3.3 changes icon: blue question or yellow exclamation
        std::string title = quest->GetTitle();
        int locale = _session->GetSessionDbLocaleIndex();
        if (locale >= 0)
            if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(questID))
                ObjectMgr::GetLocaleString(localeData->Title, locale, title);
        data.WriteBits(title.size(), 9);

        buffer << uint32(quest->GetFlags());            // 3.3.3 quest flags
        buffer << uint32(item.QuestIcon);
        buffer << int32(quest->GetQuestLevel());
        buffer.WriteString(title);
        buffer << uint32(questID);
        buffer << uint32(0);                            // quest flags2
    }

    buffer.WriteGuidBytes<2, 1>(objectGUID);
    buffer << uint32(_gossipMenu.GetMenuId());          // new 2.4.0

    for (GossipMenuItemContainer::const_iterator itr = _gossipMenu.GetMenuItems().begin(); itr != _gossipMenu.GetMenuItems().end(); ++itr)
    {
        GossipMenuItem const& item = itr->second;
        data.WriteBits(item.BoxMessage.size(), 12);
        data.WriteBits(item.Message.size(), 12);

        buffer.WriteString(item.Message);               // text for gossip item
        buffer << uint8(item.IsCoded);                  // makes pop up box password
        buffer << uint8(item.MenuItemIcon);
        buffer << uint32(item.BoxMoney);                // money required to open menu, 2.0.3
        buffer.WriteString(item.BoxMessage);            // accept text (related to money) pop up box, 2.0.3
        buffer << uint32(itr->first);
    }

    data.WriteGuidMask<3>(objectGUID);
    data.FlushBits();
    if (!buffer.empty())
        data.append(buffer);

    data.WriteGuidBytes<7, 4, 6>(objectGUID);
    data << uint32(0);                                  // friendship faction
    data.WriteGuidBytes<0, 5>(objectGUID);
    data << uint32(titleTextId);
    data.WriteGuidBytes<3>(objectGUID);

    _session->SendPacket(&data);
}

void PlayerMenu::SendCloseGossip() const
{
    WorldPacket data(SMSG_GOSSIP_COMPLETE, 0);
    _session->SendPacket(&data);
}

void PlayerMenu::SendPointOfInterest(uint32 poiId) const
{
    PointOfInterest const* poi = sObjectMgr->GetPointOfInterest(poiId);
    if (!poi)
    {
        TC_LOG_ERROR("sql", "Request to send non-existing POI (Id: %u), ignored.", poiId);
        return;
    }

    std::string iconText = poi->icon_name;
    int32 locale = _session->GetSessionDbLocaleIndex();
    if (locale >= 0)
        if (PointOfInterestLocale const* localeData = sObjectMgr->GetPointOfInterestLocale(poiId))
            ObjectMgr::GetLocaleString(localeData->IconName, locale, iconText);

    WorldPacket data(SMSG_GOSSIP_POI, 4 + 4 + 4 + 4 + 4 + 10);  // guess size
    data << uint32(poi->flags);
    data << float(poi->x);
    data << float(poi->y);
    data << uint32(poi->icon);
    data << uint32(poi->data);
    data << iconText;

    _session->SendPacket(&data);
}

/*********************************************************/
/***                    QUEST SYSTEM                   ***/
/*********************************************************/

QuestMenu::QuestMenu()
{
    _questMenuItems.reserve(16);                                   // can be set for max from most often sizes to speedup push_back and less memory use
}

QuestMenu::~QuestMenu()
{
    ClearMenu();
}

void QuestMenu::AddMenuItem(uint32 QuestId, uint8 Icon)
{
    if (!sObjectMgr->GetQuestTemplate(QuestId))
        return;

    ASSERT(_questMenuItems.size() <= GOSSIP_MAX_MENU_ITEMS);

    QuestMenuItem questMenuItem;

    questMenuItem.QuestId        = QuestId;
    questMenuItem.QuestIcon      = Icon;

    _questMenuItems.push_back(questMenuItem);
}

bool QuestMenu::HasItem(uint32 questId) const
{
    for (QuestMenuItemList::const_iterator i = _questMenuItems.begin(); i != _questMenuItems.end(); ++i)
        if (i->QuestId == questId)
            return true;

    return false;
}

void QuestMenu::ClearMenu()
{
    _questMenuItems.clear();
}

void PlayerMenu::SendQuestGiverQuestList(QEmote eEmote, const std::string& Title, uint64 npcGUID)
{
    WorldPacket data(SMSG_QUESTGIVER_QUEST_LIST, 200);      // guess size
    data.WriteBits(Title.size(), 11);
    data.WriteGuidMask<0, 4>(npcGUID);

    uint32 menuCount = 0;
    for (uint32 i = 0; i < _questMenu.GetMenuItemCount(); ++i)
        if (sObjectMgr->GetQuestTemplate(_questMenu.GetItem(i).QuestId))
            ++menuCount;
    data.WriteBits(menuCount, 19);
    data.WriteGuidMask<7>(npcGUID);

    ByteBuffer buff;
    for (uint32 i = 0; i < _questMenu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& qmi = _questMenu.GetItem(i);

        uint32 questID = qmi.QuestId;

        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questID))
        {
            std::string title = quest->GetTitle();

            Player* plr = _session->GetPlayer();

            int loc_idx = _session->GetSessionDbLocaleIndex();
            if (loc_idx >= 0)
                if (QuestLocale const* ql = sObjectMgr->GetQuestLocale(questID))
                    ObjectMgr::GetLocaleString(ql->Title, loc_idx, title);

            uint32 questStatus = plr ? plr->GetQuestStatus(questID) : 0;

            if (questStatus == QUEST_STATUS_COMPLETE)
                questStatus = 3;
            else if (questStatus == QUEST_STATUS_NONE)
                questStatus = 1;
            else if (questStatus == QUEST_STATUS_INCOMPLETE)
                questStatus = 2;

            data.WriteBit(0);                               // 3.3.3 changes icon: blue question or yellow exclamation
            data.WriteBits(title.size(), 9);

            buff << uint32(0);                              // quest flags 2
            buff.WriteString(title);
            buff << int32(quest->GetQuestLevel());
            buff << uint32(questID);
            buff << uint32(quest->GetFlags());              // 3.3.3 quest flags
            buff << uint32(questStatus);
        }
    }

    data.WriteGuidMask<2, 6, 3, 5, 1>(npcGUID);
    if (!buff.empty())
    {
        data.FlushBits();
        data.append(buff);
    }

    data.WriteGuidBytes<5, 4, 0, 7, 1, 6>(npcGUID);
    data << uint32(eEmote._Emote);                         // NPC emote
    data.WriteGuidBytes<3>(npcGUID);
    data << uint32(eEmote._Delay);                         // player emote
    data.WriteString(Title);
    data.WriteGuidBytes<2>(npcGUID);

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_QUEST_LIST NPC Guid=%u", GUID_LOPART(npcGUID));
}

void PlayerMenu::SendQuestGiverStatus(uint32 questStatus, uint64 npcGUID) const
{
    WorldPacket data(SMSG_QUESTGIVER_STATUS, 4 + 8 + 1);
    data.WriteGuidMask<3, 6, 7, 1, 4, 0, 2, 5>(npcGUID);
    data.WriteGuidBytes<1, 3>(npcGUID);
    data << uint32(questStatus);
    data.WriteGuidBytes<7, 0, 4, 6, 5, 2>(npcGUID);

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_STATUS NPC Guid=%u, status=%u", GUID_LOPART(npcGUID), questStatus);
}

void PlayerMenu::SendQuestGiverQuestDetails(Quest const* quest, uint64 npcGUID, bool activateAccept, bool isAreaTrigger /*=false*/) const
{
    std::string questTitle           = quest->GetTitle();
    std::string questDetails         = quest->GetDetails();
    std::string questObjectives      = quest->GetObjectives();
    std::string questEndText         = quest->GetEndText();
    std::string questGiverTextWindow = quest->GetQuestGiverTextWindow();
    std::string questGiverTargetName = quest->GetQuestGiverTargetName();
    std::string questTurnTextWindow  = quest->GetQuestTurnTextWindow();
    std::string questTurnTargetName  = quest->GetQuestTurnTargetName();

    int32 locale = _session->GetSessionDbLocaleIndex();
    if (locale >= 0)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
        {
            ObjectMgr::GetLocaleString(localeData->Title, locale, questTitle);
            ObjectMgr::GetLocaleString(localeData->Details, locale, questDetails);
            ObjectMgr::GetLocaleString(localeData->Objectives, locale, questObjectives);
            ObjectMgr::GetLocaleString(localeData->EndText, locale, questEndText);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTextWindow, locale, questGiverTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTargetName, locale, questGiverTargetName);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTextWindow, locale, questTurnTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTargetName, locale, questTurnTargetName);
        }
    }

    WorldPacket data(SMSG_QUESTGIVER_QUEST_DETAILS, 200);           // guess size
    data << uint32(quest->GetQuestGiverPortrait());                 // 4.x
    data << uint32(quest->GetRewItemDisplayId(1));

    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
    {
        data << uint32(quest->RewardFactionId[i]);                  // reward factions ids
        data << int32(quest->RewardFactionValueId[i]);              // columnid in QuestFactionReward.dbc (zero based)?
        data << uint32(quest->RewardFactionValueIdOverride[i]);     // reward reputation override?
    }

    data << uint32(quest->GetRewItemDisplayId(2));
    data << int32(quest->GetRewOrReqMoney());
    data << uint32(quest->RewardItemId[0]);
    data << uint32(quest->RewardChoiceItemCount[5]);
    data << uint32(quest->GetRewPackageItem());
    data << uint32(quest->GetRewItemDisplayId(3));

    for (uint8 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
    {
        data << uint32(quest->RewardCurrencyId[i]);
        data << uint32(quest->RewardCurrencyCount[i]);
    }

    data << uint32(0);
    data << uint32(quest->GetRewChoiceItemDisplayId(0));
    data << uint32(quest->GetRewChoiceItemsCount());
    data << uint32(quest->RewardChoiceItemId[3]);
    data << uint32(quest->RewardChoiceItemCount[0]);
    data << uint32(0);
    data << uint32(quest->RewardItemIdCount[1]);
    data << uint32(0);                                              // quest flags 2
    data << uint32(quest->RewardChoiceItemCount[4]);
    data << uint32(quest->RewardItemIdCount[3]);
    data << uint32(quest->RewardItemId[2]);
    data << uint32(quest->GetRewSpell());
    data << uint32(quest->GetRewChoiceItemDisplayId(2));
    data << uint32(0);
    data << uint32(quest->RewardItemId[1]);
    data << uint32(quest->GetFlags());
    data << uint32(quest->GetQuestTurnInPortrait());                // 4.x
    data << uint32(quest->GetQuestId());
    data << uint32(quest->GetSuggestedPlayers());
    data << uint32(quest->RewardChoiceItemId[2]);
    data << uint32(quest->RewardChoiceItemCount[2]);
    data << uint32(0);
    data << uint32(quest->RewardChoiceItemId[0]);
    data << uint32(quest->GetRewItemDisplayId(0));
    data << uint32(quest->RewardChoiceItemCount[3]);
    data << uint32(quest->RewardItemIdCount[0]);
    data << uint32(quest->GetRewChoiceItemDisplayId(1));
    data << uint32(quest->RewardChoiceItemId[4]);
    data << uint32(quest->GetRewItemsCount());
    data << uint32(quest->RewardChoiceItemCount[1]);
    data << uint32(quest->GetRewChoiceItemDisplayId(5));
    data << uint32(quest->GetRewChoiceItemDisplayId(3));
    data << uint32(0);
    data << uint32(quest->GetRewChoiceItemDisplayId(4));
    data << uint32(quest->GetRewSpellCast());
    data << uint32(quest->RewardChoiceItemId[1]);
    data << uint32(quest->RewardChoiceItemId[5]);
    data << uint32(0);
    data << uint32(quest->RewardItemId[3]);
    data << uint32(quest->RewardItemIdCount[2]);

    bool isAutoLaunched = true;                             //activateAccept ? 1 : 0
    ObjectGuid guid2 = isAutoLaunched ? 0 : npcGUID;

    if (isAreaTrigger)
    {
        isAutoLaunched = false;
        guid2 = npcGUID;
    }

    data.WriteGuidMask<0>(npcGUID);
    data.WriteBits(questGiverTextWindow.size(), 10);
    data.WriteGuidMask<0>(guid2);
    data.WriteBit(isAreaTrigger);                           // from areatrigger
    data.WriteGuidMask<6>(guid2);
    data.WriteGuidMask<1, 7>(npcGUID);
    data.WriteBits(questTurnTargetName.size(), 8);
    data.WriteGuidMask<2>(guid2);
    data.WriteGuidMask<2>(npcGUID);
    data.WriteBit(isAutoLaunched);
    data.WriteBit(isAreaTrigger);                          // IsFinished? value is sent back to server in quest accept packet
    data.WriteGuidMask<1>(guid2);
    data.WriteBits(questTitle.size(), 9);
    data.WriteBits(questGiverTargetName.size(), 8);
    data.WriteGuidMask<4, 6, 3>(npcGUID);
    data.WriteBits(questObjectives.size(), 12);
    data.WriteGuidMask<7>(guid2);
    data.WriteBits(questTurnTextWindow.size(), 10);
    data.WriteGuidMask<4>(guid2);
    data.WriteBits(QUEST_EMOTE_COUNT, 21);
    data.WriteGuidMask<3, 5>(guid2);

    uint8 objCount = 0;
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredIdBack[i] != 0)
            ++objCount;
    data.WriteBits(objCount, 20);

    data.WriteBits(0, 22);                                  // counter
    data.WriteBits(questDetails.size(), 12);
    data.WriteGuidMask<5>(npcGUID);

    data.WriteGuidBytes<7>(npcGUID);
    data.WriteString(questGiverTextWindow);
    data.WriteGuidBytes<2>(npcGUID);
    data.WriteGuidBytes<3>(guid2);
    data.WriteString(questDetails);
    data.WriteString(questObjectives);
    data.WriteGuidBytes<2, 1>(guid2);

    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        if (!quest->RequiredIdBack[i])
            continue;

        data << uint32(quest->RequiredIdBack[i]);
        data << uint32(quest->RequiredPOI[i]);
        data << uint32(quest->RequiredIdCount[i]);
        data << uint8(quest->RequirementType[i]);
    }

    data.WriteGuidBytes<4>(npcGUID);
    data.WriteGuidBytes<5>(guid2);
    data.WriteString(questGiverTargetName);
    data.WriteGuidBytes<0, 5>(npcGUID);
    data.WriteString(questTurnTargetName);
    data.WriteGuidBytes<6>(npcGUID);

    for (uint8 i = 0; i < QUEST_EMOTE_COUNT; ++i)
    {
        data << uint32(quest->DetailsEmote[i]);
        data << uint32(quest->DetailsEmoteDelay[i]);       // DetailsEmoteDelay (in ms)
    }

    data.WriteGuidBytes<1, 3>(npcGUID);
    data.WriteGuidBytes<6, 4>(guid2);
    data.WriteString(questTitle);
    data.WriteString(questTurnTextWindow);

    data.WriteGuidBytes<7, 0>(guid2);

    _session->SendPacket(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_QUEST_DETAILS NPCGuid=%u, questid=%u", GUID_LOPART(npcGUID), quest->GetQuestId());
}

void PlayerMenu::SendQuestQueryResponse(uint32 questId) const
{
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
    {
        WorldPacket data(SMSG_QUEST_QUERY_RESPONSE, 5);
        data << uint32(questId);
        data.WriteBit(0);

        _session->SendPacket(&data);
        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUEST_QUERY_RESPONSE questid=%u, does not exist", questId);
        return;
    }

    bool hideRewards = quest->HasFlag(QUEST_FLAGS_HIDDEN_REWARDS);
    std::string questTitle = quest->GetTitle();
    std::string questDetails = quest->GetDetails();
    std::string questObjectives = quest->GetObjectives();
    std::string questEndText = quest->GetEndText();
    std::string questCompletedText = quest->GetCompletedText();
    std::string questGiverTextWindow = quest->GetQuestGiverTextWindow();
    std::string questGiverTargetName = quest->GetQuestGiverTargetName();
    std::string questTurnTextWindow = quest->GetQuestTurnTextWindow();
    std::string questTurnTargetName = quest->GetQuestTurnTargetName();

    std::string questObjectiveText[QUEST_OBJECTIVES_COUNT];
    for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        questObjectiveText[i] = quest->ObjectiveText[i];

    int32 locale = _session->GetSessionDbLocaleIndex();
    if (locale >= 0)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
        {
            ObjectMgr::GetLocaleString(localeData->Title, locale, questTitle);
            ObjectMgr::GetLocaleString(localeData->Details, locale, questDetails);
            ObjectMgr::GetLocaleString(localeData->Objectives, locale, questObjectives);
            ObjectMgr::GetLocaleString(localeData->EndText, locale, questEndText);
            ObjectMgr::GetLocaleString(localeData->CompletedText, locale, questCompletedText);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTextWindow, locale, questGiverTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTargetName, locale, questGiverTargetName);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTextWindow, locale, questTurnTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTargetName, locale, questTurnTargetName);

            for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                ObjectMgr::GetLocaleString(localeData->ObjectiveText[i], locale, questObjectiveText[i]);
        }
    }

    WorldPacket data(SMSG_QUEST_QUERY_RESPONSE, 100);           // guess size
    data << uint32(quest->GetQuestId());
    data.WriteBit(1);                                           // has data
    data.WriteBits(questTurnTextWindow.size(), 10);
    data.WriteBits(questCompletedText.size(), 11);
    data.WriteBits(questGiverTextWindow.size(), 10);
    data.WriteBits(questObjectives.size(), 12);
    data.WriteBits(questGiverTargetName.size(), 8);

    uint32 realCount = 0;
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        if (quest->RequiredIdBack[i] == 0)
            break;
        ++realCount;
    }
    data.WriteBits(realCount, 19);

    data.WriteBits(questDetails.size(), 12);
    data.WriteBits(questTitle.size(), 9);
    data.WriteBits(questTurnTargetName.size(), 8);

    for (uint8 i = 0; i < realCount; ++i)
    {
        data.WriteBits(questObjectiveText[i].size(), 8);
        data.WriteBits(0, 22);
    }

    data.WriteBits(questEndText.size(), 9);

    data << uint32(quest->GetBonusTalents());
    data << uint32(quest->GetQuestGiverPortrait());
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[3]);
    data.WriteString(questEndText);
    data << uint32(quest->GetNextQuestInChain());               // client will request this quest from NPC, if not 0
    data.WriteString(questTurnTargetName);

    for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
    {
        data << uint32(quest->RewardFactionId[i]);              // reward factions ids
        data << int32(quest->RewardFactionValueIdOverride[i]);  // unknown usage
        data << int32(quest->RewardFactionValueId[i]);          // columnid+1 QuestFactionReward.dbc?
    }

    data.WriteString(questGiverTargetName);
    data.WriteString(questDetails);

    for (uint32 i = 0; i < realCount; ++i)
    {
        data << uint32(quest->RequiredUnkFlag[i]);              // unk
        data << uint8(quest->RequirementType[i]);               // RequirementType
        data << uint32(quest->RequiredIdBack[i]);
        data << uint32(quest->RequiredPOI[i]);
        data.WriteString(questObjectiveText[i]);
        data << uint8(i);                                       // objective index
        data << uint32(quest->RequiredIdCount[i]);
        // for (uint32 i = 0; i < ..
        //      data << uint32
    }

    data << uint32(quest->GetPointMapId());
    data << uint32(quest->RequiredSourceItemId[0]);
    data << uint32(quest->GetRewardSkillId());                  // reward skill id
    data << uint32(quest->GetRewMoneyMaxLevel());               // used in XP calculation at client
    data << uint32(hideRewards ? 0 : quest->RewardItemId[1]);
    data << uint32(hideRewards ? 0 : quest->RewardItemIdCount[2]);
    data.WriteString(questGiverTextWindow);
    data << uint32(quest->GetZoneOrSort());                     // zone or sort to display in quest log
    data << quest->GetPointX();

    for (uint32 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
    {
        data << uint32(quest->RewardCurrencyCount[i]);
        data << uint32(quest->RewardCurrencyId[i]);
    }

    data << uint32(hideRewards ? 0 : quest->RewardItemId[2]);
    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(0));
    data << uint32(quest->RequiredSourceItemCount[0]);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[1]);
    data << uint32(hideRewards ? 0 : quest->RewardItemIdCount[0]);
    data << uint32(quest->GetPointY());
    data << uint32(hideRewards ? 0 : quest->RewardItemId[3]);
    data << uint32(quest->GetRewPackageItem());                 // Test, send packageId
    data.WriteString(questTitle);
    data << uint32(quest->GetRewardReputationMask());           // rep mask (unsure on what it does)
    data << uint32(quest->GetType());                           // quest type
    data << uint32(0);
    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(2));
    data << uint32(quest->GetRewardSkillPoints());              // reward skill points
    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(4));
    data << uint32(quest->GetSoundAccept());
    data << uint32(0);
    data << uint32(quest->GetQuestTurnInPortrait());            // quest turnin entry ?

    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(5));
    data.WriteString(questTurnTextWindow);
    data << uint32(hideRewards ? 0 : quest->RewardItemIdCount[3]);
    data << uint32(quest->RequiredSourceItemCount[2]);
    data << uint32(quest->RequiredSourceItemCount[1]);
    data << float(quest->GetRewHonorMultiplier());
    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(3));
    data.WriteString(questCompletedText);
    data << uint32(0);
    data << uint32(quest->RequiredSourceItemId[3]);
    data << uint32(quest->GetQuestLevel());                     // may be -1, static data, in other cases must be used dynamic level: Player::GetQuestLevel (0 is not known, but assuming this is no longer valid for quest intended for client)
    data << uint32(quest->GetXPId());                           // used for calculating rewarded experience

    data << uint32(0);                                          // quest flags 2
    data << uint32(quest->RequiredSourceItemCount[3]);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[5]);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemCount[2]);
    data << uint32(hideRewards ? 0 : quest->GetRewChoiceItemDisplayId(1));
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[0]);
    data << uint32(0);
    data << uint32(quest->GetSoundTurnIn());

    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[2]);
    data << uint32(quest->RequiredSourceItemId[1]);
    data.WriteString(questObjectives);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemId[4]);

    data << uint32(quest->GetSrcItemId());                      // source item id
    data << int32(quest->GetRewSpellCast());                    // casted spell
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemCount[0]);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemCount[3]);
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemCount[5]);
    data << uint32(hideRewards ? 0 : quest->RewardItemIdCount[1]);
    data << uint32(quest->RequiredSourceItemCount[1]);
    data << uint32(hideRewards ? 0 : quest->RewardItemId[0]);
    data << uint32(quest->GetQuestMethod());                    // Accepted values: 0, 1 or 2. 0 == IsAutoComplete() (skip objectives/details)
    data << uint32(quest->RequiredSourceItemId[2]);
    if (hideRewards)
        data << uint32(0);                                      // Hide money rewarded
    else
        data << uint32(quest->GetRewOrReqMoney());              // reward money (below max lvl)
    data << uint32(quest->GetFlags());                          // quest flags
    data << uint32(hideRewards ? 0 : quest->RewardChoiceItemCount[4]);
    data << uint32(quest->GetPointOpt());
    data << uint32(quest->GetQuestId());                        // quest id
    data << uint32(quest->GetSuggestedPlayers());               // suggested players count
    data << uint32(quest->GetRewSpell());                       // reward spell, this spell will display (icon) (casted if RewSpellCast == 0)
    data << uint32(quest->GetMinLevel());                       // min level

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUEST_QUERY_RESPONSE questid=%u", quest->GetQuestId());
}

void PlayerMenu::SendQuestGiverOfferReward(Quest const* quest, uint64 npcGUID, bool enableNext) const
{
    Player* player = _session->GetPlayer();
    std::string questTitle = quest->GetTitle();
    std::string questOfferRewardText = quest->GetOfferRewardText();
    std::string questGiverTextWindow = quest->GetQuestGiverTextWindow();
    std::string questGiverTargetName = quest->GetQuestGiverTargetName();
    std::string questTurnTextWindow = quest->GetQuestTurnTextWindow();
    std::string questTurnTargetName = quest->GetQuestTurnTargetName();

    int locale = _session->GetSessionDbLocaleIndex();
    if (locale >= 0)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
        {
            ObjectMgr::GetLocaleString(localeData->Title, locale, questTitle);
            ObjectMgr::GetLocaleString(localeData->OfferRewardText, locale, questOfferRewardText);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTextWindow, locale, questGiverTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestGiverTargetName, locale, questGiverTargetName);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTextWindow, locale, questTurnTextWindow);
            ObjectMgr::GetLocaleString(localeData->QuestTurnTargetName, locale, questTurnTargetName);
        }
    }

    WorldPacket data(SMSG_QUESTGIVER_OFFER_REWARD, 200);            // guess size
    data << uint32(quest->GetSuggestedPlayers());                   // SuggestedGroupNum
    for (uint8 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
    {
        data << uint32(quest->RewardCurrencyId[i]);
        data << uint32(quest->RewardCurrencyCount[i]);
    }
    data << uint32(quest->RewardChoiceItemCount[5]);
    data << uint32(quest->GetRewChoiceItemDisplayId(3));
    data << uint32(quest->RewardItemId[0]);
    data << uint32(quest->RewardItemIdCount[0]);

    float QuestXpRate = 1;
    if (player->GetPersonnalXpRate())
        QuestXpRate = player->GetPersonnalXpRate();
    else
        QuestXpRate = sWorld->getRate(RATE_XP_QUEST);
    data << uint32(quest->XPValue(player) * QuestXpRate);

    data << uint32(quest->GetRewChoiceItemsCount());
    data << uint32(0);
    data << uint32(quest->RewardItemId[3]);
    data << uint32(quest->RewardChoiceItemCount[1]);
    data << uint32(quest->GetRewItemDisplayId(1));
    data << uint32(quest->GetRewItemDisplayId(0));
    data << uint32(0);
    data << uint32(0);  // quest flags 2
    data << uint32(quest->RewardChoiceItemCount[0]);
    data << uint32(quest->GetRewChoiceItemDisplayId(0));


    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i) 
    {
        data << int32(quest->RewardFactionValueId[i]);              // columnid in QuestFactionReward.dbc (zero based)?
        data << uint32(quest->RewardFactionId[i]);                  // reward factions ids
        data << uint32(quest->RewardFactionValueIdOverride[i]);     // reward reputation override?
    }


    data << uint32(quest->GetQuestGiverPortrait());
    data << uint32(quest->GetRewSpellCast());
    data << uint32(quest->GetRewItemsCount());
    data << uint32(quest->GetQuestTurnInPortrait());
    data << uint32(quest->RewardItemIdCount[1]);
    data << uint32(quest->RewardItemId[2]);
    data << uint32(quest->GetRewPackageItem());
    data << uint32(quest->GetRewItemDisplayId(2));
    data << uint32(quest->GetRewChoiceItemDisplayId(2));
    data << uint32(quest->RewardChoiceItemCount[4]);
    data << uint32(quest->RewardChoiceItemId[4]);
    data << uint32(quest->RewardChoiceItemId[1]);
    data << uint32(_GUID_ENPART_3(npcGUID));                        // npc id
    data << uint32(quest->RewardChoiceItemId[3]);
    data << uint32(quest->RewardChoiceItemId[5]);
    data << uint32(quest->RewardChoiceItemCount[3]);
    data << uint32(quest->GetQuestId());
    data << uint32(quest->RewardItemIdCount[3]);
    data << uint32(quest->RewardChoiceItemCount[2]);
    data << uint32(quest->GetRewChoiceItemDisplayId(5));
    data << uint32(quest->GetRewChoiceItemDisplayId(1));
    data << uint32(0);
    data << uint32(quest->RewardChoiceItemId[0]);
    data << uint32(quest->GetRewChoiceItemDisplayId(4));
    data << uint32(quest->GetFlags());
    data << uint32(0);
    data << uint32(0);
    data << uint32(quest->RewardItemIdCount[2]);
    data << uint32(quest->RewardItemId[1]);
    data << uint32(quest->RewardChoiceItemId[2]);
    data << uint32(quest->GetRewItemDisplayId(3));
    data << uint32(0);
    data << int32(quest->GetRewOrReqMoney());

    data.WriteGuidMask<2, 6, 3, 0>(npcGUID);
    data.WriteBits(questGiverTargetName.size(), 8);
    data.WriteBit(enableNext);                              // Auto Finish
    data.WriteGuidMask<7, 5>(npcGUID);
    data.WriteBits(questGiverTextWindow.size(), 10);
    data.WriteGuidMask<1>(npcGUID);

    uint32 emoteCount = 0;
    for (uint8 i = 0; i < QUEST_EMOTE_COUNT; ++i)
    {
        if (quest->OfferRewardEmote[i] <= 0)
            continue;
        ++emoteCount;
    }
    data.WriteBits(emoteCount, 21);
    data.WriteBits(questOfferRewardText.size(), 12);
    data.WriteBits(questTurnTargetName.size(), 8);
    data.WriteBits(questTitle.size(), 9);
    data.WriteBits(questTurnTextWindow.size(), 10);
    data.WriteGuidMask<4>(npcGUID);

    data.WriteGuidBytes<4, 7>(npcGUID);
    data.WriteString(questTitle);
    data.WriteString(questTurnTargetName);
    data.WriteGuidBytes<5>(npcGUID);
    for (uint8 i = 0; i < QUEST_EMOTE_COUNT; ++i)
    {
        if (quest->OfferRewardEmote[i] <= 0)
            continue;

        data << uint32(quest->OfferRewardEmoteDelay[i]);
        data << uint32(quest->OfferRewardEmote[i]);
    }

    data.WriteGuidBytes<1, 0>(npcGUID);
    data.WriteString(questGiverTargetName);
    data.WriteGuidBytes<6, 3, 2>(npcGUID);
    data.WriteString(questGiverTextWindow);
    data.WriteString(questOfferRewardText);
    data.WriteString(questTurnTextWindow);

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_OFFER_REWARD NPCGuid=%u, questid=%u", GUID_LOPART(npcGUID), quest->GetQuestId());
}

void PlayerMenu::SendQuestGiverRequestItems(Quest const* quest, uint64 npcGUID, bool canComplete, bool closeOnCancel) const
{
    // We can always call to RequestItems, but this packet only goes out if there are actually
    // items.  Otherwise, we'll skip straight to the OfferReward

    std::string questTitle = quest->GetTitle();
    std::string requestItemsText = quest->GetRequestItemsText();

    int32 locale = _session->GetSessionDbLocaleIndex();
    if (locale >= 0)
    {
        if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(quest->GetQuestId()))
        {
            ObjectMgr::GetLocaleString(localeData->Title, locale, questTitle);
            ObjectMgr::GetLocaleString(localeData->RequestItemsText, locale, requestItemsText);
        }
    }

    if (!quest->GetReqItemsCount() && canComplete)
    {
        SendQuestGiverOfferReward(quest, npcGUID, true);
        return;
    }

    WorldPacket data(SMSG_QUESTGIVER_REQUEST_ITEMS, 150);   // guess size
    data.WriteGuidMask<6, 0, 1>(npcGUID);
    data.WriteBits(quest->GetReqCurrencyCount(), 21);
    data.WriteGuidMask<4, 5>(npcGUID);
    data.WriteBits(requestItemsText.size(), 12);
    data.WriteGuidMask<3>(npcGUID);

    data.WriteBits(quest->GetReqItemsCount(), 20);          // objectives count
    data.WriteBit(closeOnCancel);                           // Close Window after cancel
    data.WriteGuidMask<7, 2>(npcGUID);
    data.WriteBits(questTitle.size(), 9);

    data << uint32(0);                                      // quest flags 2
    data << uint32(quest->GetRewOrReqMoney() < 0 ? -quest->GetRewOrReqMoney() : 0);
    data.WriteGuidBytes<3>(npcGUID);
    data << uint32(quest->GetFlags());                      // 3.3.3 questFlags
    data.WriteGuidBytes<4, 5>(npcGUID);
    if (canComplete)
        data << uint32(quest->GetCompleteEmote());
    else
        data << uint32(quest->GetIncompleteEmote());
    data.WriteGuidBytes<2>(npcGUID);
    data << uint32(quest->GetSuggestedPlayers());           // SuggestedGroupNum
    data.WriteString(requestItemsText);

    for (uint8 i = 0; i < QUEST_REQUIRED_CURRENCY_COUNT; ++i)
    {
        if (!quest->RequiredCurrencyId[i])
            continue;

        data << uint32(quest->RequiredCurrencyId[i]);
        data << uint32(quest->RequiredCurrencyCount[i]);
    }
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        if (!quest->RequiredItemId[i])
            continue;

        data << uint32(quest->RequiredItemId[i]);
        data << uint32(quest->RequiredItemCount[i]);

        if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(quest->RequiredItemId[i]))
            data << uint32(itemTemplate->DisplayInfoID);
        else
            data << uint32(0);
    }

    data.WriteGuidBytes<7, 1>(npcGUID);

    /*
    v17 = ((unsigned int)v16 >> 1) & 1
        && ((unsigned int)v16 >> 2) & 1
        && ((unsigned int)v16 >> 3) & 1
        && ((unsigned int)v16 >> 4) & 1
        && ((unsigned int)v16 >> 6) & 1;
    */
    if (!canComplete)                                       // Experimental; there are 6 similar flags, if any of them
        data << uint32(0);                                  // is 0 player can't complete quest (still unknown meaning)
    else
        data << uint32(0x7E);

    data << uint32(quest->GetQuestId());
    data << uint32(0);              // emote delay

    data.WriteGuidBytes<0>(npcGUID);
    data.WriteString(questTitle);
    data.WriteGuidBytes<6>(npcGUID);
    data << uint32(_GUID_ENPART_3(npcGUID));                // npc id

    _session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_QUESTGIVER_REQUEST_ITEMS NPCGuid=%u, questid=%u", GUID_LOPART(npcGUID), quest->GetQuestId());
}
