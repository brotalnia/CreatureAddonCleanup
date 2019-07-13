// This tool cleans up the creature addon tables.
// Author: brotalnia
//

#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <array>
#include <set>
#include <sstream>

#include "Database\Database.h"

Database GameDb;

std::string MakeConnectionString()
{
    std::string mysql_host;
    std::string mysql_port;
    std::string mysql_user;
    std::string mysql_pass;
    std::string mysql_db;

    printf("Host: ");
    getline(std::cin, mysql_host);
    if (mysql_host.empty())
        mysql_host = "127.0.0.1";

    printf("Port: ");
    getline(std::cin, mysql_port);
    if (mysql_port.empty())
        mysql_port = "3306";

    printf("User: ");
    getline(std::cin, mysql_user);
    if (mysql_user.empty())
        mysql_user = "root";

    printf("Password: ");
    getline(std::cin, mysql_pass);
    if (mysql_pass.empty())
        mysql_pass = "root";

    printf("Database: ");
    getline(std::cin, mysql_db);
    if (mysql_db.empty())
        mysql_db = "mangos";

    return mysql_host + ";" + mysql_port + ";" + mysql_user + ";" + mysql_pass + ";" + mysql_db;
}

struct CreatureAddon
{
    uint32 guid;
    uint32 patch;
    uint32 creatureId;
    uint32 mount;
    uint32 bytes1;
    uint32 b2_0_sheath;
    uint32 b2_1_flags;
    uint32 emote;
    uint32 moveflags;
    std::string auras;
    bool operator==(const CreatureAddon &an) const
    {
        return mount == an.mount && bytes1 == an.bytes1 && b2_0_sheath == an.b2_0_sheath && b2_1_flags == an.b2_1_flags &&
               emote == an.emote && moveflags == an.moveflags && auras == an.auras;
    }
};

std::vector<CreatureAddon> vAddonEntry;
std::vector<CreatureAddon> vAddonGuid;
std::unordered_map<uint32, uint32> mSpawns;
std::unordered_map<uint32, uint32> mPatches;

CreatureAddon* FindAddonByEntry(uint32 entry)
{
    for (auto& itr : vAddonEntry)
    {
        if (itr.creatureId == entry)
            return &itr;
    }
    return nullptr;
}

CreatureAddon* FindAddonByGuid(uint32 guid)
{
    for (auto& itr : vAddonGuid)
    {
        if (itr.guid == guid)
            return &itr;
    }
    return nullptr;
}

int main()
{
    printf("\nEnter your database connection info.\n");
    std::string const connection_string = MakeConnectionString();

    printf("\nConnecting to database.\n");
    if (!GameDb.Initialize(connection_string.c_str()))
    {
        printf("\nError: Cannot connect to database!\n");
        getchar();
        return 1;
    }    

    printf("Loading addon by entry.\n");
    if (std::shared_ptr<QueryResult> result = GameDb.Query("SELECT * FROM `creature_template_addon`"))
    {
        do
        {
            DbField* pFields = result->fetchCurrentRow();

            CreatureAddon newEntry;
            newEntry.creatureId = pFields[0].getUInt32();
            newEntry.patch = pFields[1].getUInt32();
            newEntry.mount = pFields[2].getUInt32();
            newEntry.bytes1 = pFields[3].getUInt32();
            newEntry.b2_0_sheath = pFields[4].getUInt32();
            newEntry.b2_1_flags = pFields[5].getUInt32();
            newEntry.emote = pFields[6].getUInt32();
            newEntry.moveflags = pFields[7].getUInt32();
            newEntry.auras = pFields[8].getCppString();

            vAddonEntry.push_back(newEntry);
        } while (result->NextRow());
    }
    printf("Loaded %u rows.\n", vAddonEntry.size());

    printf("Loading creature spawn data.\n");
    if (std::shared_ptr<QueryResult> result = GameDb.Query("SELECT `guid`, `id` FROM `creature`"))
    {
        do
        {
            DbField* pFields = result->fetchCurrentRow();

            uint32 guid = pFields[0].getUInt32();
            uint32 id = pFields[1].getUInt32();

            mSpawns.insert(std::make_pair(guid, id));
        } while (result->NextRow());
    }
    printf("Loaded %u rows.\n", mSpawns.size());

    printf("Loading addon by guid.\n");
    if (std::shared_ptr<QueryResult> result = GameDb.Query("SELECT * FROM `creature_addon`"))
    {
        do
        {
            DbField* pFields = result->fetchCurrentRow();

            CreatureAddon newEntry;
            newEntry.guid = pFields[0].getUInt32();
            newEntry.creatureId = mSpawns.find(newEntry.guid)->second;
            newEntry.patch = pFields[1].getUInt32();
            newEntry.mount = pFields[2].getUInt32();
            newEntry.bytes1 = pFields[3].getUInt32();
            newEntry.b2_0_sheath = pFields[4].getUInt32();
            newEntry.b2_1_flags = pFields[5].getUInt32();
            newEntry.emote = pFields[6].getUInt32();
            newEntry.moveflags = pFields[7].getUInt32();
            newEntry.auras = pFields[8].getCppString();

            vAddonGuid.push_back(newEntry);
        } while (result->NextRow());
    }
    printf("Loaded %u rows.\n", vAddonGuid.size());

    printf("Loading minimum patch for creatures.\n");
    if (std::shared_ptr<QueryResult> result = GameDb.Query("SELECT `entry`, min(`patch`) FROM `creature_template` GROUP BY `entry`"))
    {
        do
        {
            DbField* pFields = result->fetchCurrentRow();

            uint32 entry = pFields[0].getUInt32();
            uint32 patch = pFields[1].getUInt32();

            mPatches.insert(std::make_pair(entry, patch));
        } while (result->NextRow());
    }
    printf("Loaded %u rows.\n", mPatches.size());

    std::ofstream myfile("creature_addon_cleanup.sql");
    if (!myfile.is_open())
        return 1;

    std::set<uint32> guidsToDelete;
    std::unordered_map<uint32, std::vector<CreatureAddon>> guidOnlyAddons;

    for (auto const& i : vAddonGuid)
    {
        if (CreatureAddon* addonByEntry = FindAddonByEntry(i.creatureId))
        {
            if (i == *addonByEntry)
                guidsToDelete.insert(i.guid);
        }
        else
        {
            guidOnlyAddons[i.creatureId].push_back(i);
        }
    }

    std::unordered_map<uint32, std::vector<CreatureAddon>> guidOnlyAddons2 = guidOnlyAddons;

    // exclude creatures that have differing addon entries by guid
    for (auto const& i : guidOnlyAddons)
    {
        if (i.second.size() == 1)
            continue;

        bool different = false;
        for (uint32 idx = 0; idx < i.second.size(); idx++)
        {
            if (idx == 0)
                continue;
            if (!(i.second[idx] == i.second[idx - 1]))
            {
                different = true;
                break;
            }
        }
        if (different)
            guidOnlyAddons2.erase(i.first);
    }

    for (auto const& i : mSpawns)
    {
        // exclude creatures that only have addon data for some spawns
        if (!FindAddonByGuid(i.first))
            guidOnlyAddons2.erase(i.second);
    }

    std::vector<CreatureAddon> newTemplateAddons;

    for (auto const& i : guidOnlyAddons2)
    {
        if (i.second.empty())
            exit(1);

        CreatureAddon newAddon;
        for (auto const& j : i.second)
        {
            guidsToDelete.insert(j.guid);
            newAddon = j;
        }

        if (newAddon.patch == 0)
            newAddon.patch = mPatches.find(newAddon.creatureId)->second;

        newTemplateAddons.push_back(newAddon);
    }

    myfile << "-- Removing not needed addon entries by spawn guid.\n";
    myfile << "DELETE FROM `creature_addon` WHERE `guid` IN (";
    uint32 index = 0;
    for (auto i : guidsToDelete)
    {
        if (index != 0)
            myfile << ", ";
        myfile << i;
        index++;
    }
    myfile << ");\n";

    myfile << "-- Define addon entries by creature id instead of guid if they are all the same.\n";
    myfile << "INSERT INTO `creature_template_addon` (`entry`, `patch`, `mount`, `bytes1`, `b2_0_sheath`, `b2_1_flags`, `emote`, `moveflags`, `auras`) VALUES \n";
    index = 0;
    for (auto const& i : newTemplateAddons)
    {
        if (index != 0)
            myfile << ",\n";
        myfile << "(" << i.creatureId << ", " << i.patch << ", " << i.mount << ", " << i.bytes1 << ", " << i.b2_0_sheath << ", " << i.b2_1_flags << ", " << i.emote << ", " << i.moveflags << ", '" << i.auras << "')";
        index++;
    }
    myfile << ";\n";

    printf("Done.\n");
    fflush(stdin);
    getchar();
    
    GameDb.Uninitialise();
    return 0;
}

