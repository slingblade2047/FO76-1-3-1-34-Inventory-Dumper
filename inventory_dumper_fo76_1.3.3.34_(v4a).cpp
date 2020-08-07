// Fallout 76 player stash dumper
// test Jun 17, 2020
// Version 4 - writes CSV format delimited by;
// changed column order to apply filters in Excel

// missing function to print first line of CSV
// sprintf_s ("Type; Level; Weight; Count; Item; Prefix; Maior; Minor;";
// need to manually add the first line or an external TXT
// manually add this row to CSV to import it into Excel
// Location; Type; Level; Weight; Count; Item; Perk1; Perk2; Perk3; Note1; Note2; Note3
// or paste all the text of the -CSV into an existing Excel sheet

// to do: optional printing of the ItemID
// to do: automatic search for LocalPlayerOffset and LocalSTASHOffset for new versions of Fallot76.exe

#include <fstream>
#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>

HWND sHwnd = NULL;
DWORD sPid = NULL;
HANDLE sHandle = NULL;
DWORD64 sAddress = NULL;
char sWindow[] = "Fallout76";
char sModule[] = "Fallout76.exe";

// for Fallout 76 Update 1.3.3.24. (Aug-6-2020)
// Just change the local player offset to 0x05B49F78
// DWORD LocalPlayerOffset = 0x059AC798;//1.3.0.23
// DWORD LocalPlayerOffset = 0x05B49F78;//1.3.0.26
// DWORD LocalPlayerOffset = 0x05B0A458;//1.3.1.34
   DWORD LocalPlayerOffset = 0x05B45BB0;//1.3.3.24

DWORD CharacterNameOffset = 0x59A3188;//1.3.0.23
// LocalSTASHOffset in the linked source is incorrect, 0xC28 is the correct offset.
// DWORD LocalSTASHOffset = 0xC24;//1.3.0.23
   DWORD LocalSTASHOffset = 0xC28;//1.3.1.26

char InventoryName[0x200] = "Inventory_Player.csv";//This can be changed
bool ShowWeapons = true;
bool ShowArmor = true;
bool ShowAid = true;
bool ShowMods = true;
bool ShowJunk = true;
bool ShowMisc = true;
bool ShowUtility = true;
bool ShowKeys = true;
bool ShowHolotapes = true;
bool ShowPlans = true;
bool ShowNotes = true;
bool ShowAmmo = true;
bool ShowUnknown = false;//Missing items
bool NotifyUnknown = false;	//Creates a MessageBox if unknown rtti names are found
bool ShowNormalWeapons = true;	//Non-legendary weapons, requires ShowWeapons to be enabled
bool ShowNormalArmor = true;	//Non-legendary armor, requires ShowArmor to be enabled
bool ShowCharacterName = true;	//Replace [Player] with character name (if found)
bool ShowLocation = true;	//Add [Player]/[STASH]
bool ShowType = true;		//Add item type
bool ShowModifier = true;	//Add legendary modifier
bool ShowCount = true;		//Add inventory count (regular equipment/items only)
bool ShowWeight = true;		//Add item weight
bool ShowLevel = true;		//Add item level
bool ShowArmorType = true;	//Add [Light]/[Sturdy]/[Heavy] to armor
bool ShowSTASH = true;		//Include items in STASH
bool ShowEquipped = true;	//Include equipped items
bool ShowFavorited = true;	//Include favorited items
bool TargetLegendary = true;	//Set the formids for the legendary effects below, matches will be marked by [Tagged]
DWORD TargetFormid[]		//0x00000000 is skipped
{
	0x00000000,//Skipped
	0x00000000,//Skipped
	0x00000000,//Skipped
};

bool CharacterNameUpdated = false;
char CharacterName[0x300] = { '\0' };
std::ofstream InventoryStream;
size_t UnknownTracker = 0;

bool SingleKeyToggle(DWORD KeyCode, bool &KeyToggle)
{
	if (GetAsyncKeyState(KeyCode))
	{
		if (!KeyToggle)
		{
			KeyToggle = true;
			return KeyToggle;
		}
	}
	else
	{
		if (KeyToggle)
		{
			KeyToggle = false;
			return KeyToggle;
		}
	}

	return false;
}

bool DoubleKeyToggle(DWORD KeyCodeA, DWORD KeyCodeB, bool &KeyToggle)
{
	if (GetAsyncKeyState(KeyCodeA))
	{
		return SingleKeyToggle(KeyCodeB, KeyToggle);
	}

	if (GetAsyncKeyState(KeyCodeB))
	{
		return SingleKeyToggle(KeyCodeA, KeyToggle);
	}

	return false;
}

HWND GetHwnd()
{
//	printf("[Set %s as the foreground window]\n", sWindow);
//	printf("CTRL+INSERT: Get HWND of foreground window\n");

	printf("[Start %s to catch the current player]\n", sWindow);
	printf("CTRL + INSERT: press during the game, generates a TXT file with the inventory\n");

	bool SelectionKeyToggle = false;
	for (int i = 0; i < 5000; i++)
	{
		Sleep(1);
		if (DoubleKeyToggle(VK_CONTROL, VK_INSERT, SelectionKeyToggle))
		{
			system("cls");
			return GetForegroundWindow();
		}
	}

	return NULL;
}

DWORD GetPid(HWND sHwnd)
{
	DWORD pBuffer;
	GetWindowThreadProcessId(sHwnd, &pBuffer);
	return pBuffer;
}

DWORD64 GetModuleAddress64(const char *sModule, DWORD sPid)
{
	HANDLE mHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, sPid);
	if (!mHandle) return NULL;

	MODULEENTRY32 mEntry;
	mEntry.dwSize = sizeof(mEntry);

	while (Module32Next(mHandle, &mEntry))
	{
		if (!strcmp(mEntry.szModule, sModule))
		{
			CloseHandle(mHandle);
			return (DWORD64)mEntry.modBaseAddr;
		}
	}

	CloseHandle(mHandle);
	return NULL;
}

HANDLE GetHandle(DWORD sPid)
{
	return OpenProcess(PROCESS_VM_READ, false, sPid);
}

bool Valid(DWORD64 ptr)
{
	if (ptr < 0x7FFF || ptr > 0x7FFFFFFFFFFF) return false;
	else return true;
}

bool RPM(HANDLE sHandle, DWORD64 src, void *dst, size_t Size)
{
	return ReadProcessMemory(sHandle, (void*)(src), dst, Size, NULL);
}

class Inventory
{
public:
	DWORD64 vtable;//0x0
	BYTE Padding0008[0x58];
	DWORD64 ItemArrayPtr;//0x60
	DWORD64 ItemArrayEnd;//0x68
};

class Item
{
public:
	DWORD64 ReferencePtr;//0x0
	char PaddingA[0x8];
	DWORD64 DescriptionPtr;//0x10
	char PaddingB[0x8];
	DWORD64 Iterations;//0x20
	unsigned char EquipFlagA;//0x28
	unsigned char EquipFlagB;//0x29
	char PaddingD[0x2];
	DWORD ItemId;//0x2C
	unsigned char FavoriteFlag;//0x30
	char PaddingE[0x3];
	float ItemWeight;//0x34
};

class ItemSize
{
public:
	DWORD64 Ptr;
	DWORD Size;
	char PaddingA[0x4];
};

class Reference
{
public:
	DWORD64 vtable;//0x0
	char PaddingA[0x10];
	unsigned char CheckA;//0x18
	char PaddingB[0x7];
	DWORD Formid;//0x20
	char PaddingC[0x1B4];
	unsigned char CheckB;//0x1D8
	char PaddingD[0x1F];
	unsigned char CheckC;//0x1F8
};

class Mod
{
public:
	DWORD64 ModListPtr;//0x0
	int ModListSize;//0x8
};

class EquipmentExtra
{
public:
	char PaddingA[0x10];
	DWORD64 ModListBufferPtr;//0x10
	unsigned char PaddingB[0x4];
	short EquipmentLevel;//0x1C
};

class OMOD
{
public:
	DWORD Flag;
	DWORD Formid;
	char Effect[0x200];
};

/*
Flag:
0x00000000 = Unused
0x00000001 = Legendary Armor Prefix
0x00000002 = Legendary Armor Major
0x00000003 = Legendary Armor Minor
0x00000004 = Legendary Weapon Prefix
0x00000005 = Legendary Weapon Major
0x00000006 = Legendary Weapon Minor
0x00000007 = Light Armor
0x00000008 = Sturdy Armor
0x00000009 = Heavy Armor
*/
OMOD ObjectModification[]
{
	{ { 0x00000001 }, { 0x00529A0F }, { "When incapacitated, gain a 50% chance to revive yourself with a Stimpak, once every minute" } },
	{ { 0x00000001 }, { 0x00524146 }, { "Blend with the environment while sneaking and not moving" } },
	{ { 0x00000001 }, { 0x00524147 }, { "Being hit in melee generates a Stealth Field once per 30 seconds" } },
	{ { 0x00000001 }, { 0x00529A09 }, { "Slowly regenerate health while not in combat" } },
	{ { 0x00000001 }, { 0x004F6D7D }, { "-15% damage from animals" } },
	{ { 0x00000001 }, { 0x004F6D7C }, { "-15% damage from Mirelurks and bugs" } },
	{ { 0x00000001 }, { 0x004F6D7E }, { "-15% damage from ghouls" } },
	{ { 0x00000001 }, { 0x004F6D78 }, { "75% chance to reduce damage by 8% from Players." } },
	{ { 0x00000001 }, { 0x004F6D7F }, { "-15% damage from robots" } },
	{ { 0x00000001 }, { 0x004EE548 }, { "-15% damage from Scorched" } },
	{ { 0x00000001 }, { 0x004F6D80 }, { "-15% damage from Super Mutants" } },
	{ { 0x00000001 }, { 0x0052414A }, { "Gain up to +3 to all stats (except END) when low health" } },
	{ { 0x00000001 }, { 0x00521915 }, { "Automatically use a Stimpak when hit while health is 25% or less, once every 60 seconds" } },
	{ { 0x00000001 }, { 0x00521914 }, { "Grants up to +35 Energy and Damage Resistance, the lower your health" } },
	{ { 0x00000001 }, { 0x00529A0C }, { "+10 Damage and Energy Resist if you are mutated" } },
	{ { 0x00000001 }, { 0x00524143 }, { "Damage and Energy Resist increase with the night and decrease with the day" } },
	{ { 0x00000001 }, { 0x00529A05 }, { "Grants up to +35 Energy and Damage Resistance, the higher your health." } },
	{ { 0x00000001 }, { 0x00529A14 }, { "Weighs 90% less and does not count as armor for the Chameleon mutation." } },
	{ { 0x00000002 }, { 0x00527F75 }, { "Increases Action Point refresh speed" } },
	{ { 0x00000002 }, { 0x00527F72 }, { "+25% environmental disease resistance" } },
	{ { 0x00000002 }, { 0x00527F6E }, { "+25 poison resistance" } },
	{ { 0x00000002 }, { 0x00527F6F }, { "+25 radiation resistance" } },
	{ { 0x00000002 }, { 0x004F6D85 }, { "+1 Agility" } },
	{ { 0x00000002 }, { 0x004F6D83 }, { "+1 Charisma" } },
	{ { 0x00000002 }, { 0x004F6D82 }, { "+1 Endurance" } },
	{ { 0x00000002 }, { 0x004F6D84 }, { "+1 Intelligence" } },
	{ { 0x00000002 }, { 0x004F6D86 }, { "+1 Luck" } },
	{ { 0x00000002 }, { 0x004F6D81 }, { "+1 Perception" } },
	{ { 0x00000002 }, { 0x004EE54E }, { "+1 Strength" } },
	{ { 0x00000003 }, { 0x0052BDBA }, { "50% more durability than normal" } },
	{ { 0x00000003 }, { 0x00527F76 }, { "Reduces damage while blocking by 15%" } },
	{ { 0x00000003 }, { 0x00527F77 }, { "75% chance to reduce damage by 15% while sprinting." } },
	{ { 0x00000003 }, { 0x004EE54C }, { "75% chance to reduce damage by 15% while standing still" } },
	{ { 0x00000003 }, { 0x00527F79 }, { "Reduces falling damage by 50%" } },
	{ { 0x00000003 }, { 0x00527F7B }, { "Increases size of sweet-spot while picking locks" } },
	{ { 0x00000003 }, { 0x0052BDBC }, { "Receive 15% less limb damage" } },
	{ { 0x00000003 }, { 0x0052BDB7 }, { "Become harder to detect while sneaking" } },
	{ { 0x00000003 }, { 0x00527F7A }, { "Grants the ability to breathe underwater" } },
	{ { 0x00000003 }, { 0x0052BDB4 }, { "Ammo weight reduced by 20%" } },
	{ { 0x00000003 }, { 0x0052BDB5 }, { "Food, drink, and chem weights reduced by 20%" } },
	{ { 0x00000003 }, { 0x0052BDB6 }, { "Junk item weights reduced by 20%" } },
	{ { 0x00000003 }, { 0x00527F78 }, { "Weapon weights reduced by 20%" } },
	{ { 0x00000004 }, { 0x004F6D77 }, { "If not in combat, +100% VATS accuracy at +50% AP cost" } },
	{ { 0x00000004 }, { 0x005281B4 }, { "Ignores 50% of your target's armor" } },
	{ { 0x00000004 }, { 0x004F6AAB }, { "More damage the more your withdrawal effects" } },
	{ { 0x00000004 }, { 0x004F6AA5 }, { "Double damage if target is full health" } },
	{ { 0x00000004 }, { 0x004F6AA0 }, { "Does more damage the lower your health is" } },
	{ { 0x00000004 }, { 0x004F6AAE }, { "Damage increases with the night and decreases with the day" } },
	{ { 0x00000004 }, { 0x004F6AA7 }, { "More damage the lower your Damage Resistance" } },
	{ { 0x00000004 }, { 0x005281B8 }, { "Reduce your target's damage output by 20% for 3s" } },
	{ { 0x00000004 }, { 0x004F577D }, { "Damage increased after each consecutive hit on the same target" } },
	{ { 0x00000004 }, { 0x004F577A }, { "+30% damage to animals" } },
	{ { 0x00000004 }, { 0x001F81EB }, { "+30% damage to Mirelurks and bugs" } },
	{ { 0x00000004 }, { 0x004F5779 }, { "+30% damage to ghouls" } },
	{ { 0x00000004 }, { 0x004F5770 }, { "+10% damage to Players" } },
	{ { 0x00000004 }, { 0x004F577C }, { "+30% damage to robots" } },
	{ { 0x00000004 }, { 0x004ED02B }, { "+30% damage to Scorched" } },
	{ { 0x00000004 }, { 0x004F577B }, { "+30% damage to super mutants" } },
	{ { 0x00000004 }, { 0x005299F5 }, { "Damage increased by 10% if you are mutated" } },
	{ { 0x00000004 }, { 0x004F6AA1 }, { "+50% more damage when your target is below 40% health" } },
	{ { 0x00000004 }, { 0x004392CD }, { "Double ammo capacity" } },//Legacy Effect
	{ { 0x00000004 }, { 0x004F6AB1 }, { "Quadruple ammo capacity" } },
	{ { 0x00000004 }, { 0x00527F8B }, { "V.A.T.S. crits will heal you and your group" } },
	{ { 0x00000004 }, { 0x004F6D76 }, { "Shoots an additional projectile" } },
	{ { 0x00000004 }, { 0x00527F84 }, { "Gain brief health regeneration when you hit an enemy" } },
	{ { 0x00000005 }, { 0x004ED02C }, { "+50% limb damage" } },
	{ { 0x00000005 }, { 0x005299F9 }, { "Bashing damage increased by 40%" } },
	{ { 0x00000005 }, { 0x0052414B }, { "V.A.T.S. critical shots do +50% damage" } },
	{ { 0x00000005 }, { 0x0052414E }, { "+10% damage while aiming" } },
	{ { 0x00000005 }, { 0x004F5771 }, { "Bullets explode for area damage." } },
	{ { 0x00000005 }, { 0x00425E28 }, { "Bullets explode for area damage." } },
	{ { 0x00000005 }, { 0x0052414F }, { "25% faster fire rate" } },
	{ { 0x00000005 }, { 0x00524153 }, { "+33% VATS hit chance" } },
	{ { 0x00000005 }, { 0x001A7C39 }, { "Reflects 50% of melee damage back while blocking" } },
	{ { 0x00000005 }, { 0x001A7BE2 }, { "40% more power attack damage" } },
	{ { 0x00000005 }, { 0x001A7BDA }, { "40% faster swing speed" } },
	{ { 0x00000006 }, { 0x0037F7D9 }, { "50% more durability than normal" } },
	{ { 0x00000006 }, { 0x0052414C }, { "Your V.A.T.S. critical meter fills 15% faster" } },
	{ { 0x00000006 }, { 0x004ED02E }, { "Faster movement speed while aiming" } },
	{ { 0x00000006 }, { 0x00524150 }, { "15% faster reload" } },
	{ { 0x00000006 }, { 0x004F5777 }, { "+250 Damage Resistance while reloading" } },
	{ { 0x00000006 }, { 0x004F5772 }, { "+50 Damage Resistance while aiming" } },
	{ { 0x00000006 }, { 0x005299FA }, { "+1 Perception" } },
	{ { 0x00000006 }, { 0x00524154 }, { "25% less V.A.T.S. Action Point cost" } },
	{ { 0x00000006 }, { 0x005253FB }, { "Take 15% less damage while blocking" } },
	{ { 0x00000006 }, { 0x001A7BD3 }, { "Take 40% less damage while power attacking" } },
	{ { 0x00000006 }, { 0x005299FD }, { "+1 Endurance" } },
	{ { 0x00000006 }, { 0x005299FC }, { "+1 Strength" } },
	{ { 0x00000006 }, { 0x005299FB }, { "+1 Agility" } },
	{ { 0x00000006 }, { 0x00524152 }, { "90% reduced weight" } },
	{ { 0x00000007 }, { 0x00182E5B }, { "Light" } },
	{ { 0x00000007 }, { 0x00182E5C }, { "Light" } },
	{ { 0x00000007 }, { 0x00182E69 }, { "Light" } },
	{ { 0x00000007 }, { 0x00184009 }, { "Light" } },
	{ { 0x00000007 }, { 0x00184013 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018401C }, { "Light" } },
	{ { 0x00000007 }, { 0x00184BD3 }, { "Light" } },
	{ { 0x00000007 }, { 0x00184BE6 }, { "Light" } },
	{ { 0x00000007 }, { 0x00184BED }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B16B }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B178 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B188 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B19F }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B1A0 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B1A5 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B1A7 }, { "Light" } },
	{ { 0x00000007 }, { 0x0018B1A8 }, { "Light" } },
	{ { 0x00000007 }, { 0x0050972E }, { "Light" } },
	{ { 0x00000007 }, { 0x00509731 }, { "Light" } },
	{ { 0x00000007 }, { 0x00509732 }, { "Light" } },
	{ { 0x00000008 }, { 0x00182E6A }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00182E6B }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00182E6C }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0018400A }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00184012 }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0018401B }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00184BD2 }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00184BE5 }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00184BEC }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0018B16C }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0018B17C }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0018B18C }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0050972C }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x0050972D }, { "Sturdy" } },
	{ { 0x00000008 }, { 0x00509730 }, { "Sturdy" } },
	{ { 0x00000009 }, { 0x00182E6D }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00182E6E }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00182E6F }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0018400B }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00184011 }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0018401A }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00184BE4 }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00184BEB }, { "Heavy" } },
	{ { 0x00000009 }, { 0x00184BF0 }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0018B16D }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0018B17E }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0018B190 }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0050972A }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0050972B }, { "Heavy" } },
	{ { 0x00000009 }, { 0x0050972F }, { "Heavy" } },
};

DWORD64 GetrttiNamePtr(DWORD64 src)
{
	DWORD64 BufferA;
	if (!RPM(sHandle, src, &BufferA, sizeof(BufferA))) return 0;
	if (!Valid(BufferA)) return 0;

	DWORD64 BufferB;
	if (!RPM(sHandle, BufferA - 0x8, &BufferB, sizeof(BufferB))) return 0;
	if (!Valid(BufferB)) return 0;

	DWORD Offset;
	if (!RPM(sHandle, BufferB + 0xC, &Offset, sizeof(Offset))) return 0;
	if (Offset == 0 || Offset > 0x7FFFFFFF) return 0;

	return sAddress + Offset + 0x10;
}

DWORD64 GetvtablePtr(DWORD64 src, DWORD64 Max, const char *rttiName)
{
	if (src > Max) return 0;
	size_t Iteration = (Max - src) / sizeof(DWORD64);

	for (size_t i = 0; i < Iteration; i++)
	{
		DWORD64 PtrBuffer;
		if (!RPM(sHandle, src + i * 0x8, &PtrBuffer, sizeof(PtrBuffer))) continue;
		if (!Valid(PtrBuffer)) continue;

		DWORD64 rttiNamePtr = GetrttiNamePtr(PtrBuffer);
		if (!Valid(rttiNamePtr)) continue;

		char rttiNameCheck[0x200];
		if (!RPM(sHandle, rttiNamePtr, &rttiNameCheck, sizeof(rttiNameCheck))) continue;
		if (!strcmp(rttiNameCheck, rttiName)) return PtrBuffer;
	}

	return 0;
}

int CheckFlag(DWORD Flag)
{
	if (Flag == 0x00000001) return 1;
	else if (Flag == 0x00000002) return 1;
	else if (Flag == 0x00000003) return 1;
	else if (Flag == 0x00000004) return 1;
	else if (Flag == 0x00000005) return 1;
	else if (Flag == 0x00000006) return 1;
	else if (Flag == 0x00000007) return 2;
	else if (Flag == 0x00000008) return 2;
	else if (Flag == 0x00000009) return 2;
	else return 0;
}

bool CheckLegendary(DWORD *LegendaryId, size_t LegendaryIdSize)
{
	if (sizeof(TargetFormid) / sizeof(DWORD) != LegendaryIdSize)
	{
		return false;
	}

	bool CheckFormids = false;
	for (size_t i = 0; i < LegendaryIdSize; i++)
	{
		if (TargetFormid[i] != 0x00000000)
		{
			CheckFormids = true;
			break;
		}
	}

	if (!CheckFormids) return false;

	bool *Match = new bool[LegendaryIdSize];
	for (size_t i = 0; i < LegendaryIdSize; i++)
	{
		if (TargetFormid[i] == 0x00000000)
		{
			Match[i] = true;
		}
		else
		{
			Match[i] = false;
		}
	}

	for (size_t i = 0; i < LegendaryIdSize; i++)
	{
		if (!Match[i])
		{
			for (size_t c = 0; c < LegendaryIdSize; c++)
			{
				if (LegendaryId[c] != 0xDEAD)
				{
					if (TargetFormid[i] == ObjectModification[LegendaryId[c]].Formid)
					{
						Match[i] = true;
					}
				}
			}
		}
	}

	bool LegendaryResult = true;
	for (size_t i = 0; i < LegendaryIdSize; i++)
	{
		if (!Match[i])
		{
			LegendaryResult = false;
			break;
		}
	}

	delete[]Match;

	return LegendaryResult;
}

bool CreateItemType(int Type)
{
	switch (Type)
	{
	case 10:
//		InventoryStream << "Type: Weapon\n";
		InventoryStream << "Weapon;";
		return true;
	case 20:
//		InventoryStream << "Type: Armor\n";
		InventoryStream << "Armor;";
		return true;
	case 30:
//		InventoryStream << "Type: Aid\n";
		InventoryStream << "Aid;";
		return true;
	case 40:
//		InventoryStream << "Type: Mod\n";
		InventoryStream << "Mod;";
		return true;
	case 50:
//		InventoryStream << "Type: Junk\n";
		InventoryStream << "Junk;";
		return true;
	case 60:
//		InventoryStream << "Type: Misc\n";
		InventoryStream << "Misc;";
		return true;
	case 70:
//		InventoryStream << "Type: Utility\n";
		InventoryStream << "Utility;";
		return true;
	case 80:
//		InventoryStream << "Type: Key\n";
		InventoryStream << "Key;";
		return true;
	case 90:
//		InventoryStream << "Type: Holotape\n";
		InventoryStream << "Holotape;";
		return true;
	case 100:
//		InventoryStream << "Type: Plan\n";
		InventoryStream << "Plan;";
		return true;
	case 110:
//		InventoryStream << "Type: Note\n";
		InventoryStream << "Note;";
		return true;
	case 120:
//		InventoryStream << "Type: Ammo\n";
		InventoryStream << "Ammo;";
		return true;
	case 999:
//		InventoryStream << "Type: Unknown\n";
		InventoryStream << "Unknown;";
		return true;
	default:
		return false;
	}
}

bool GetLegendaryModifier(DWORD LegendaryId)
{
	switch (ObjectModification[LegendaryId].Flag)
	{
	case 0x00000001:
//		InventoryStream << "Prefix: ";
		InventoryStream << " ";
		return true;
	case 0x00000002:
//		InventoryStream << "Major: ";
		InventoryStream << " ";
		return true;
	case 0x00000003:
//		InventoryStream << "Minor: ";
		InventoryStream << " ";
		return true;
	case 0x00000004:
//		InventoryStream << "Prefix: ";
		InventoryStream << " ";
		return true;
	case 0x00000005:
//		InventoryStream << "Major: ";
		InventoryStream << " ";
		return true;
	case 0x00000006:
//		InventoryStream << "Minor: ";
		InventoryStream << " ";
		return true;
	default:
		return false;
	}
}

// Print Riga 1 - Header del CSV

// missing function to print first line cdel CSV
// sprintf_s ("Type; Level; Weight; Count; Item; Prefix; Maior; Minor;";

// Item Type WEAPON - Print Location; Type; Level; Weight; NO-Count; Item ; PRefix; Maior; Minor

bool CreateItemTextA(Item ItemData, EquipmentExtra EquipmentExtraData, Mod ModData, char *EquipmentName, bool InStash, int Type)
{
	bool LegendaryFound = false;
	DWORD *ModFormid = new DWORD[ModData.ModListSize / 2];
	if (RPM(sHandle, ModData.ModListPtr, &*ModFormid, sizeof(DWORD) * ModData.ModListSize / 2))
	{
		int LegendaryTracker = 0;
		DWORD LegendaryId[]
		{
			{ 0xDEAD },
			{ 0xDEAD },
			{ 0xDEAD },
		};

		int ArmorTypeId = 0xDEAD;

		for (int i = 0; i < ModData.ModListSize / 8; i++)
		{
			for (int c = 0; c < sizeof(ObjectModification) / sizeof(OMOD); c++)
			{
				if (ModFormid[i * 2] == ObjectModification[c].Formid)
				{
					if (CheckFlag(ObjectModification[c].Flag) == 1)
					{
						if (LegendaryTracker < sizeof(LegendaryId) / sizeof(DWORD))
						{
							if (!LegendaryFound) LegendaryFound = true;
							LegendaryId[LegendaryTracker] = c;
							LegendaryTracker++;
						}
					}
					else if (CheckFlag(ObjectModification[c].Flag) == 2)
					{
						if (ArmorTypeId == 0xDEAD)
						{
							ArmorTypeId = c;
						}
					}
				}
			}
		}

		if (LegendaryFound)
		{
			bool LegendaryChecked = false;
			if (TargetLegendary) LegendaryChecked = CheckLegendary(LegendaryId, sizeof(LegendaryId) / sizeof(DWORD));

			if (ShowLocation)
			{
				if (LegendaryChecked)
				{
					if (InStash)
					{
						char STASHText[0x400];
//						if (CharacterNameUpdated) sprintf_s(STASHText, "[STASH]%s[Tagged]\n", CharacterName);
//						else sprintf_s(STASHText, "[STASH][Tagged]\n");
						if (CharacterNameUpdated) sprintf_s(STASHText, "[STASH]%s[Tagged]\n", CharacterName);
						else sprintf_s(STASHText, "[STASH][Tagged]\n");
						InventoryStream << STASHText;
					}
					else
					{
						char PlayerText[0x400];
//						if (CharacterNameUpdated) sprintf_s(PlayerText, "%s[Tagged]\n", CharacterName);
//						else sprintf_s(PlayerText, "[Player][Tagged]\n");
						if (CharacterNameUpdated) sprintf_s(PlayerText, "%s[Tagged]\n", CharacterName);
						else sprintf_s(PlayerText, "[Player][Tagged]\n");
						InventoryStream << PlayerText;
					}
				}
				else
				{
					if (InStash)
					{
						char STASHText[0x400];
//						if (CharacterNameUpdated) sprintf_s(STASHText, "[STASH]%s\n", CharacterName);
//						else sprintf_s(STASHText, "[STASH]\n");
						if (CharacterNameUpdated) sprintf_s(STASHText, "STASH%s;", CharacterName);
						else sprintf_s(STASHText, "STASH;");
						InventoryStream << STASHText;
					}
					else
					{
						char PlayerText[0x400];
//						if (CharacterNameUpdated) sprintf_s(PlayerText, "%s\n", CharacterName);
//						else sprintf_s(PlayerText, "[Player]\n");
						if (CharacterNameUpdated) sprintf_s(PlayerText, "%s;", CharacterName);
						else sprintf_s(PlayerText, "Player;");

						InventoryStream << PlayerText;
					}
				}
			}
			else if (LegendaryChecked) InventoryStream << "[Tagged]\n";

			if (ShowType)
			{
				CreateItemType(Type);
			}

			if (ShowLevel)
			{
				char EquipmentLevelText[0x200];
				//				sprintf_s(EquipmentLevelText, "Level: %hi\n", EquipmentExtraData.EquipmentLevel);
				sprintf_s(EquipmentLevelText, "%hi;", EquipmentExtraData.EquipmentLevel);
				InventoryStream << EquipmentLevelText;
			}

			if (ShowWeight)
			{
				char EquipmentWeightText[0x200];
				//				sprintf_s(EquipmentWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
				sprintf_s(EquipmentWeightText, "%.2f;", ItemData.ItemWeight);
				InventoryStream << EquipmentWeightText;
			}

//  insert ";" for empthy colon "Count"
			InventoryStream << ";";


			if (ShowArmorType && ArmorTypeId != 0xDEAD)
			{
				char EquipmentNameText[0x300];
//				sprintf_s(EquipmentNameText, "Item: [%s] %s\n", ObjectModification[ArmorTypeId].Effect, EquipmentName);
				sprintf_s(EquipmentNameText, "[%s] %s;", ObjectModification[ArmorTypeId].Effect, EquipmentName);
				InventoryStream << EquipmentNameText;
			}
			else
			{
				char EquipmentNameText[0x300];
//				sprintf_s(EquipmentNameText, "Item: %s\n", EquipmentName);
				sprintf_s(EquipmentNameText, "%s;", EquipmentName);
				InventoryStream << EquipmentNameText;
			}

			if (LegendaryId[0] != 0xDEAD)
			{
				if (ShowModifier) GetLegendaryModifier(LegendaryId[0]);
				char LegendaryEffectTextA[sizeof(ObjectModification[LegendaryId[0]].Effect) + 0x100];
//				sprintf_s(LegendaryEffectTextA, "%s\n", ObjectModification[LegendaryId[0]].Effect);
				sprintf_s(LegendaryEffectTextA, "%s;", ObjectModification[LegendaryId[0]].Effect);
				InventoryStream << LegendaryEffectTextA;
			}

			if (LegendaryId[1] != 0xDEAD)
			{
				if (ShowModifier) GetLegendaryModifier(LegendaryId[1]);
				char LegendaryEffectTextB[sizeof(ObjectModification[LegendaryId[1]].Effect) + 0x100];
//				sprintf_s(LegendaryEffectTextB, "%s\n", ObjectModification[LegendaryId[1]].Effect);
				sprintf_s(LegendaryEffectTextB, "%s;", ObjectModification[LegendaryId[1]].Effect);
				InventoryStream << LegendaryEffectTextB;
			}
			if (LegendaryId[2] != 0xDEAD)
			{
				if (ShowModifier) GetLegendaryModifier(LegendaryId[2]);
				char LegendaryEffectTextC[sizeof(ObjectModification[LegendaryId[2]].Effect) + 0x100];
//				sprintf_s(LegendaryEffectTextC, "%s\n", ObjectModification[LegendaryId[2]].Effect);
				sprintf_s(LegendaryEffectTextC, "%s;", ObjectModification[LegendaryId[2]].Effect);
				InventoryStream << LegendaryEffectTextC;
			}

// 			if (ShowWeight)
//			{
//				char EquipmentWeightText[0x200];
  //				sprintf_s(EquipmentWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
//				sprintf_s(EquipmentWeightText, "%.2f;", ItemData.ItemWeight);
//				InventoryStream << EquipmentWeightText;
//			}

//			if (ShowLevel)
//			{
//				char EquipmentLevelText[0x200];
// //				sprintf_s(EquipmentLevelText, "Level: %hi\n", EquipmentExtraData.EquipmentLevel);
//				sprintf_s(EquipmentLevelText, "%hi;", EquipmentExtraData.EquipmentLevel);
//				InventoryStream << EquipmentLevelText;
//			}

//			if (ShowType)
//			{
//				CreateItemType(Type);
//			}

			InventoryStream << "\n";
		}
	}
	delete[]ModFormid;

	return LegendaryFound;
}

// Item Type ARMOR - Print Location; Type; Level; Weight; NO-Count; Item ; Prefix; Maior; Minor
bool CreateItemTextB(Item ItemData, EquipmentExtra EquipmentExtraData, Mod ModData, char *EquipmentName, bool InStash, int Type)
{
	DWORD *ModFormid = new DWORD[ModData.ModListSize / 2];
	if (RPM(sHandle, ModData.ModListPtr, &*ModFormid, sizeof(DWORD) * ModData.ModListSize / 2))
	{
		int ArmorTypeId = 0xDEAD;

		for (int i = 0; i < ModData.ModListSize / 8; i++)
		{
			for (int c = 0; c < sizeof(ObjectModification) / sizeof(OMOD); c++)
			{
				if (ModFormid[i * 2] == ObjectModification[c].Formid)
				{
					if (CheckFlag(ObjectModification[c].Flag) == 2)
					{
						if (ArmorTypeId == 0xDEAD)
						{
							ArmorTypeId = c;
							break;
						}
					}
				}
			}
		}

		if (ShowLocation)
		{
			if (InStash)
			{
				char STASHText[0x400];
//				if (CharacterNameUpdated) sprintf_s(STASHText, "[STASH]%s\n", CharacterName);
//				else sprintf_s(STASHText, "[STASH]\n");
				if (CharacterNameUpdated) sprintf_s(STASHText, "STASH %s;", CharacterName);
				else sprintf_s(STASHText, "STASH;");
				InventoryStream << STASHText;
			}
			else
			{
				char PlayerText[0x400];
//				if (CharacterNameUpdated) sprintf_s(PlayerText, "%s\n", CharacterName);
//				else sprintf_s(PlayerText, "[Player]\n");
				if (CharacterNameUpdated) sprintf_s(PlayerText, "%s;", CharacterName);
				else sprintf_s(PlayerText, "Player;");
				InventoryStream << PlayerText;
			}
		}

		if (ShowType)
		{
			CreateItemType(Type);
		}

		if (ShowLevel)
		{
			char EquipmentLevelText[0x200];
			//			sprintf_s(EquipmentLevelText, "Level: %hi\n", EquipmentExtraData.EquipmentLevel);
			sprintf_s(EquipmentLevelText, "%hi;", EquipmentExtraData.EquipmentLevel);
			InventoryStream << EquipmentLevelText;
		}

		if (ShowWeight)
		{
			char EquipmentWeightText[0x200];
			//			sprintf_s(EquipmentWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
			sprintf_s(EquipmentWeightText, "%.2f;", ItemData.ItemWeight);
			InventoryStream << EquipmentWeightText;
		}

//  insert ";" for empthy colon "Count"
		InventoryStream << ";";


		if (ShowArmorType && ArmorTypeId != 0xDEAD)
		{
			char EquipmentNameText[0x300];
//			sprintf_s(EquipmentNameText, "Item: [%s] %s\n", ObjectModification[ArmorTypeId].Effect, EquipmentName);
			sprintf_s(EquipmentNameText, "[%s] %s;", ObjectModification[ArmorTypeId].Effect, EquipmentName);
			InventoryStream << EquipmentNameText;
		}
		else
		{
			char EquipmentNameText[0x300];
//			sprintf_s(EquipmentNameText, "Item: %s\n", EquipmentName);
			sprintf_s(EquipmentNameText, "%s;", EquipmentName);
			InventoryStream << EquipmentNameText;
		}

//		if (ShowWeight)
//		{
//			char EquipmentWeightText[0x200];
  //			sprintf_s(EquipmentWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
//			sprintf_s(EquipmentWeightText, "%.2f;", ItemData.ItemWeight);
//			InventoryStream << EquipmentWeightText;
//		}

//		if (ShowLevel)
//		{
//			char EquipmentLevelText[0x200];
//			sprintf_s(EquipmentLevelText, "Level: %hi\n", EquipmentExtraData.EquipmentLevel);
//			sprintf_s(EquipmentLevelText, "%hi;", EquipmentExtraData.EquipmentLevel);
//			InventoryStream << EquipmentLevelText;
//		}

//		if (ShowType)
//		{
//			CreateItemType(Type);
//		}

		InventoryStream << "\n";
	}
	delete[]ModFormid;

	return true;
}

// Item Type MISC and other - Print Location; Type; NO-Level; Weight ; Count; Item ; NO-PRefix; NO-Maior; NO-Minor

bool CreateItemTextC(Item ItemData, DWORD64 Size, char *ItemName, bool InStash, int Type)
{
	if (ShowLocation)
	{
		if (InStash)
		{
			char STASHText[0x400];
//			if (CharacterNameUpdated) sprintf_s(STASHText, "[STASH]%s\n", CharacterName);
//			else sprintf_s(STASHText, "[STASH]\n");
			if (CharacterNameUpdated) sprintf_s(STASHText, "STASH%s;", CharacterName);
			else sprintf_s(STASHText, "STASH;");
			InventoryStream << STASHText;
		}
		else
		{
			char PlayerText[0x400];
//			if (CharacterNameUpdated) sprintf_s(PlayerText, "%s\n", CharacterName);
//			else sprintf_s(PlayerText, "[Player]\n");
			if (CharacterNameUpdated) sprintf_s(PlayerText, "%s;", CharacterName);
			else sprintf_s(PlayerText, "Player;");
			InventoryStream << PlayerText;
		}
	}

	if (ShowType)
	{
		CreateItemType(Type);
	}

//  insert ";" for empthy colon "Level"
	InventoryStream << ";";

	if (ShowWeight)
	{
		char ItemWeightText[0x200];
		//		sprintf_s(ItemWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
		sprintf_s(ItemWeightText, "%.2f;", ItemData.ItemWeight);
		InventoryStream << ItemWeightText;
	}

	if (ShowCount)
	{
		char ItemCountText[0x200];
		//		sprintf_s(ItemCountText, "Count: %llu\n", Size);
		sprintf_s(ItemCountText, "%llu;", Size);
		InventoryStream << ItemCountText;
	}
	
	char ItemNameText[0x300];
//	sprintf_s(ItemNameText, "Item: %s\n", ItemName);
	sprintf_s(ItemNameText, "%s;", ItemName);
	InventoryStream << ItemNameText;

//	if (ShowWeight)
//	{
//		char ItemWeightText[0x200];
  //		sprintf_s(ItemWeightText, "Weight: %.2f\n", ItemData.ItemWeight);
//		sprintf_s(ItemWeightText, "%.2f;", ItemData.ItemWeight);
//		InventoryStream << ItemWeightText;
//	}

//	if (ShowCount)
//	{
//		char ItemCountText[0x200];
  //		sprintf_s(ItemCountText, "Count: %llu\n", Size);
//		sprintf_s(ItemCountText, "%llu;", Size);
//		InventoryStream << ItemCountText;
//	}

//	if (ShowType)
//	{
//		CreateItemType(Type);
//	}

	InventoryStream << "\n";

	return true;
}

bool UpdateEquipmentData(Item ItemData, bool InStash, int Flag, int Type)
{
	if (!Valid(ItemData.DescriptionPtr)) return false;
	if (!ShowEquipped && ItemData.EquipFlagA == 1) return false;
	if (!ShowFavorited && ItemData.FavoriteFlag <= 0xB) return false;

	DWORD64 EquipmentBufferA;
	if (!RPM(sHandle, ItemData.DescriptionPtr, &EquipmentBufferA, sizeof(EquipmentBufferA))) return false;
	if (!Valid(EquipmentBufferA)) return false;

	DWORD64 EquipmentBufferB;
	if (!RPM(sHandle, EquipmentBufferA + 0x10, &EquipmentBufferB, sizeof(EquipmentBufferB))) return false;
	if (!Valid(EquipmentBufferB)) return false;

	DWORD64 EquipmentBufferC;
	if (!RPM(sHandle, EquipmentBufferA + 0x20, &EquipmentBufferC, sizeof(EquipmentBufferC))) return false;
	if (!Valid(EquipmentBufferC)) return false;

	DWORD64 ExtraDataPtr = GetvtablePtr(EquipmentBufferB, EquipmentBufferC, ".?AVBGSObjectInstanceExtra@@");
	if (!Valid(ExtraDataPtr)) return false;

	EquipmentExtra EquipmentExtraData;
	if (!RPM(sHandle, ExtraDataPtr, &EquipmentExtraData, sizeof(EquipmentExtraData))) return false;
	if (!Valid(EquipmentExtraData.ModListBufferPtr)) return false;

	Mod ModData;
	if (!RPM(sHandle, EquipmentExtraData.ModListBufferPtr, &ModData, sizeof(ModData))) return false;
	if (!Valid(ModData.ModListPtr)) return false;
	if (ModData.ModListSize < 8) return false;

	DWORD64 NameBufferA = GetvtablePtr(EquipmentBufferB, EquipmentBufferC, ".?AVExtraTextDisplayData@@");
	if (!Valid(NameBufferA)) return false;

	DWORD64 NameBufferB;
	if (!RPM(sHandle, NameBufferA + 0x10, &NameBufferB, sizeof(NameBufferB))) return false;
	if (!Valid(NameBufferB)) return false;

	DWORD64 NameBufferC;
	if (!RPM(sHandle, NameBufferB + 0x10, &NameBufferC, sizeof(NameBufferC))) return false;
	if (!Valid(NameBufferC)) return false;

	int EquipmentNameLength;
	if (!RPM(sHandle, NameBufferC + 0x10, &EquipmentNameLength, sizeof(EquipmentNameLength))) return false;
	if (EquipmentNameLength > 0x200) return false;

	char EquipmentName[0x200];
	EquipmentName[0] = '\0';
	if (!RPM(sHandle, NameBufferC + 0x18, &EquipmentName, EquipmentNameLength)) return false;
	if (EquipmentName[0] == '\0') return false;
	if (EquipmentName[EquipmentNameLength] != '\0') EquipmentName[EquipmentNameLength] = '\0';

	if (!CreateItemTextA(ItemData, EquipmentExtraData, ModData, EquipmentName, InStash, Type))
	{
		if (Flag == 1)
		{
			if (!ShowNormalWeapons) return false;
			return CreateItemTextB(ItemData, EquipmentExtraData, ModData, EquipmentName, InStash, Type);
		}
		else if (Flag == 2)
		{
			if (!ShowNormalArmor) return false;
			return CreateItemTextB(ItemData, EquipmentExtraData, ModData, EquipmentName, InStash, Type);
		}
		else return false;
	}
	else return true;
}

DWORD64 GetItemSize(Item ItemData)
{
	size_t Iterations = (ItemData.Iterations - ItemData.DescriptionPtr) / sizeof(ItemSize);
	if (!Iterations || Iterations > 0x7FFF) return 0;

	ItemSize *ItemCountData = new ItemSize[Iterations];
	if (!RPM(sHandle, ItemData.DescriptionPtr, &*ItemCountData, Iterations * sizeof(ItemSize)))
	{
		delete[]ItemCountData;
		return 0;
	}

	size_t Count = 0;
	for (size_t c = 0; c < Iterations; c++)
	{
		Count += ItemCountData[c].Size;
	}

	delete[]ItemCountData;

	return Count;
}

bool GetReferenceName(Item ItemData, DWORD Offset, char *ItemName)
{
	DWORD64 NameBufferA;
	if (!RPM(sHandle, ItemData.ReferencePtr + Offset, &NameBufferA, sizeof(NameBufferA))) return false;
	if (!Valid(NameBufferA)) return 0;

	DWORD64 NameBufferB;
	if (!RPM(sHandle, NameBufferA + 0x10, &NameBufferB, sizeof(NameBufferB))) return false;
	if (!Valid(NameBufferB)) return 0;

	int ItemNameLength;
	if (!RPM(sHandle, NameBufferB + 0x10, &ItemNameLength, sizeof(ItemNameLength))) return false;
	if (ItemNameLength > 0x200) return 0;

	char ItemNameCheck[0x200];
	ItemNameCheck[0] = '\0';
	if (!RPM(sHandle, NameBufferB + 0x18, &ItemNameCheck, ItemNameLength)) return false;
	if (ItemNameCheck[0] == '\0') return false;
	if (ItemNameCheck[ItemNameLength] != '\0') ItemNameCheck[ItemNameLength] = '\0';
	memcpy(&*ItemName, ItemNameCheck, ItemNameLength);

	return true;
}

bool UpdateReferenceData(Item ItemData, bool InStash, int Type)
{
	if (!ShowEquipped && ItemData.EquipFlagA == 1) return false;
	if (!ShowFavorited && ItemData.FavoriteFlag <= 0xB) return false;

	DWORD64 Size;
	if (!Valid(ItemData.DescriptionPtr) || ItemData.Iterations == 0)
	{
		Size = 0;
	}
	else
	{
		Size = GetItemSize(ItemData);
	}

	if (Size == 0) Size = 1;

	char ItemName[0x200];
	for (int i = 0; i < 0x200; i++) ItemName[i] = '\0';

	bool NameUpdated = false;
	if (!NameUpdated) NameUpdated = GetReferenceName(ItemData, 0x98, ItemName);
	if (!NameUpdated) NameUpdated = GetReferenceName(ItemData, 0x108, ItemName);
	if (!NameUpdated) return false;

	return CreateItemTextC(ItemData, Size, ItemName, InStash, Type);
}

bool CreateUnknownText(Item ItemData, bool InStash, char *rttiName, int Type)
{
	if (rttiName[0] == '\0') return false;
	InventoryStream << rttiName << "\n";
	UnknownTracker++;
	return UpdateReferenceData(ItemData, InStash, Type);
}

bool UpdateItemData(Item ItemData, bool InStash)
{
	if (!Valid(ItemData.ReferencePtr)) return false;

	DWORD64 rttiNamePtr = GetrttiNamePtr(ItemData.ReferencePtr);
	if (rttiNamePtr == 0) return false;

	char rttiName[0x200];
	if (!RPM(sHandle, rttiNamePtr, &rttiName, sizeof(rttiName))) return false;

	Reference ReferenceData;
	if (!RPM(sHandle, ItemData.ReferencePtr, &ReferenceData, sizeof(ReferenceData))) return false;
	if (ReferenceData.Formid == 0 || ReferenceData.Formid == 0x00021B3B/*Pip-Boy 2000*/) return false;

	int Type = 999;
	if (!strcmp(rttiName, ".?AVTESObjectWEAP@@"))
	{
		if (!ShowWeapons) return false;

		Type = 10;
		if (!UpdateEquipmentData(ItemData, InStash, 1, Type))
		{
			if (!ShowNormalWeapons) return false;
			return UpdateReferenceData(ItemData, InStash, Type);
		}
	}
	else if (!strcmp(rttiName, ".?AVTESObjectARMO@@"))
	{
		if (!ShowArmor) return false;

		Type = 20;
		if (!UpdateEquipmentData(ItemData, InStash, 2, Type))
		{
			if (!ShowNormalArmor) return false;
			return UpdateReferenceData(ItemData, InStash, Type);
		}
	}
	else if (!strcmp(rttiName, ".?AVAlchemyItem@@"))
	{
		if (!ShowAid) return false;

		Type = 30;
		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVTESObjectMISC@@"))
	{
		if (ReferenceData.CheckC == 1)
		{
			if (((ReferenceData.CheckA >> 7) & 1) == 1)
			{
				if (!ShowMods) return false;
				Type = 40;
			}
			else
			{
				if (!ShowJunk) return false;
				Type = 50;
			}
		}
		else
		{
			if (!ShowMisc) return false;
			Type = 60;
		}

		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVTESUtilityItem@@"))
	{
		if (!ShowUtility) return false;

		Type = 70;
		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVTESKey@@"))
	{
		if (!ShowKeys) return false;

		Type = 80;
		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVBGSNote@@"))
	{
		if (!ShowHolotapes) return false;

		Type = 90;
		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVTESObjectBOOK@@"))
	{
		if (((ReferenceData.CheckB >> 5) & 1) == 1)
		{
			if (!ShowPlans) return false;
			Type = 100;
		}
		else
		{
			if (!ShowNotes) return false;
			Type = 110;
		}

		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (!strcmp(rttiName, ".?AVTESAmmo@@"))
	{
		if (!ShowAmmo) return false;

		Type = 120;
		return UpdateReferenceData(ItemData, InStash, Type);
	}
	else if (ShowUnknown)
	{
		if (NotifyUnknown)
		{
			return CreateUnknownText(ItemData, InStash, rttiName, Type);
		}
		else
		{
			return UpdateReferenceData(ItemData, InStash, Type);
		}
	}
	else
	{
		return false;
	}

	return true;
}

bool UpdateInventory(DWORD64 InventoryPtr, bool InStash)
{
	DWORD64 InventoryDataPtr;
	if (!RPM(sHandle, InventoryPtr + 0x80, &InventoryDataPtr, sizeof(InventoryDataPtr))) return false;
	if (!Valid(InventoryDataPtr)) return false;

	Inventory InventoryData;
	if (!RPM(sHandle, InventoryDataPtr, &InventoryData, sizeof(InventoryData))) return false;
	if (!Valid(InventoryData.ItemArrayPtr) || InventoryData.ItemArrayEnd < InventoryData.ItemArrayPtr) return false;

	size_t ItemArraySize = (InventoryData.ItemArrayEnd - InventoryData.ItemArrayPtr) / sizeof(Item);
	if (!ItemArraySize || ItemArraySize > 0x7FFF) return false;

	Item *InventoryItemData = new Item[ItemArraySize];
	if (RPM(sHandle, InventoryData.ItemArrayPtr, &*InventoryItemData, sizeof(Item) * ItemArraySize))
	{
		for (size_t i = 0; i < ItemArraySize; i++) UpdateItemData(InventoryItemData[i], InStash);
	}
	delete[]InventoryItemData;

	return true;
}

bool GetPlayerInventory()
{
	DWORD64 LocalPlayer;
	if (!RPM(sHandle, sAddress + LocalPlayerOffset, &LocalPlayer, sizeof(LocalPlayer))) return false;
	if (!Valid(LocalPlayer)) return false;
	if (!UpdateInventory(LocalPlayer, false)) return false;

	if (!ShowSTASH) return true;

	DWORD64 StashPtr;
	if (!RPM(sHandle, LocalPlayer + LocalSTASHOffset, &StashPtr, sizeof(StashPtr))) return false;
	if (!Valid(StashPtr)) return false;
	if (!UpdateInventory(StashPtr, true)) return false;

	return true;
}

bool GetCharacterName()
{
	DWORD64 CharacterBufferA;
	if (!RPM(sHandle, sAddress + CharacterNameOffset, &CharacterBufferA, sizeof(CharacterBufferA))) return false;
	if (!Valid(CharacterBufferA)) return false;

	DWORD64 CharacterBufferB;
	if (!RPM(sHandle, CharacterBufferA + 0x10, &CharacterBufferB, sizeof(CharacterBufferB))) return false;
	if (!Valid(CharacterBufferB)) return false;

	int LengthCheck;
	if (!RPM(sHandle, CharacterBufferB + 0x10, &LengthCheck, sizeof(LengthCheck))) return false;
	if (LengthCheck > 26) return false;

	char CharacterNameBuffer[0x200];
	if (!RPM(sHandle, CharacterBufferB + 0x18, &CharacterNameBuffer, sizeof(CharacterNameBuffer))) return false;
	if (CharacterNameBuffer[0] == '\0') return false;
	CharacterNameBuffer[LengthCheck] = '\0';

	sprintf_s(CharacterName, "[%s]", CharacterNameBuffer);

	return true;
}

int main()
{
	sHwnd = GetHwnd();
	if (sHwnd == NULL) return 1;

	sPid = GetPid(sHwnd);
	if (sPid == NULL) return 2;

	sAddress = GetModuleAddress64(sModule, sPid);
	if (sAddress == NULL) return 4;

	sHandle = GetHandle(sPid);
	if (sHandle == NULL) return 8;

	if (ShowCharacterName) CharacterNameUpdated = GetCharacterName();

	InventoryStream.std::ofstream::open(InventoryName);
	GetPlayerInventory();
	InventoryStream.std::ofstream::close();
	CloseHandle(sHandle);

	if (NotifyUnknown && UnknownTracker > 0)
	{
		char TrackerText[0x200];
		sprintf_s(TrackerText, "Unknown item type(s) found: %llu", UnknownTracker);
		MessageBox(NULL, TrackerText, "Missing item type(s) found\n", NULL);
	}

	return 0;
}
