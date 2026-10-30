// Source/DoTKingdom/Private/Kingdom.cpp
#include "DoTKingdom/Kingdom.h"

#include "DoTSim/Hash.h"
#include "DoTSim/Json.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace dot
{
namespace kingdom
{
namespace
{
constexpr int64_t kUnlimited = std::numeric_limits<int64_t>::max() / 4;
constexpr int64_t kSecondsPerHour = 3600;

int64_t CeilDiv(int64_t A, int64_t B)
{
	return B > 0 ? (A + B - 1) / B : 0;
}

/** Linear interpolation over (x, y) points, integer only. */
int64_t Interpolate(const std::vector<std::pair<int64_t, int64_t>>& Points, int64_t X)
{
	if (X <= 0 || Points.empty())
	{
		return 0;
	}
	for (size_t I = 1; I < Points.size(); ++I)
	{
		const auto& A = Points[I - 1];
		const auto& B = Points[I];
		if (X <= B.first)
		{
			const int64_t Span = B.first - A.first;
			return Span > 0 ? A.second + CeilDiv((X - A.first) * (B.second - A.second), Span) : B.second;
		}
	}
	// Past the last point: continue the last slope.
	const auto& A = Points.size() >= 2 ? Points[Points.size() - 2] : Points.back();
	const auto& B = Points.back();
	const int64_t Span = B.first - A.first;
	return Span > 0 ? B.second + CeilDiv((X - B.first) * (B.second - A.second), Span) : B.second;
}

int64_t FloorDiv(int64_t A, int64_t B)
{
	return A >= 0 ? A / B : -((-A + B - 1) / B);
}

/** Gold and Food can be bought with gems; gems, stones, shards and VP cannot. */
bool OnlyBuyableMissing(const ResArray& Missing)
{
	for (int I = 0; I < kResCount; ++I)
	{
		const Res R = static_cast<Res>(I);
		if (R != Res::Gold && R != Res::Food && Missing[static_cast<size_t>(I)] > 0)
		{
			return false;
		}
	}
	return true;
}

uint64_t Mix(uint64_t Seed, uint64_t Counter)
{
	uint64_t Data[2] = {Seed, Counter};
	return XXHash64(Data, sizeof(Data), 0x6b696e67646f6dULL);
}

/** A time shortened by a relic bonus in tenths of a percent (rounded up, never below one second). */
int64_t Shorten(int64_t Sec, int64_t Bonus)
{
	return Bonus > 0 && Sec > 0 ? std::max<int64_t>(1, CeilDiv(Sec * (1000 - std::min<int64_t>(Bonus, 1000)), 1000)) : Sec;
}
} // namespace

const char* ToString(Error E)
{
	static const char* const Names[] = {"none", "bad_id", "max_level", "castle_too_low", "need_requirement", "need_expansion", "locked",
		"limit_reached", "no_builder", "not_enough", "bad_placement", "busy", "not_busy", "nothing", "storage_full", "need_stones",
		"no_land_slot", "no_attack", "queue_full", "researching", "too_many_squads", "no_troops", "titan_recovering", "not_ready",
		"roster_full", "not_max_level", "wrong_relic", "same_relic", "inventory_full", "need_vp", "alliance_full", "in_alliance", "no_alliance"};
	const int I = static_cast<int>(E);
	return I >= 0 && I < static_cast<int>(Error::Count) ? Names[I] : "?";
}

Kingdom::Kingdom(const KingdomContent& InContent) : C(InContent) {}

void Kingdom::NewGame(int64_t Now, uint64_t InSeed)
{
	Time = Now;
	Res_ = C.startResources;
	StonesAt = Now;
	BuildingList.clear();
	LandList.clear();
	WorldList.clear();
	ExpansionCount = 1;
	ExtraSlots = 0;
	NextId = 1;
	Seed = InSeed;
	WorldCounter = 0;
	Pending = 0;
	PendingIsCapture = false;
	SeasonNo = SeasonOf(Now);
	Last = SeasonResult();
	ResetAlliances();
	for (const StartBuilding& S : C.startBuildings)
	{
		Building B;
		B.id = NextId++;
		B.def = S.building;
		B.level = std::min(S.level, C.buildings[static_cast<size_t>(S.building)].MaxLevel());
		B.x = S.x;
		B.y = S.y;
		B.lastAt = Now;
		BuildingList.push_back(B);
	}
	Res_[static_cast<size_t>(ResIndex(Res::Stones))] = std::min(Amount(Res::Stones), Capacity(Res::Stones));
	ResetArmy();
	ResetRaids(Now);
	SiteReady.assign(C.sites.list.size(), 0);
	SiteCounter = 0;
	MissionStarList.assign(C.campaigns.acts.size(), {});
	ChestMask.assign(C.campaigns.acts.size(), 0);
	QuestDay = -1;
	QuestCounter = 0;
	RollQuests();
	FillWorld();
}

void Kingdom::ResetArmy()
{
	TroopLevels.assign(C.troops.size(), 1);
	Army = C.startArmy;
	Army.resize(C.troops.size(), 0);
	Queue.clear();
	QueueStart = Time;
	bQueueBlocked = false;
	Research = -1;
	ResearchFrom = 0;
	ResearchUntil = 0;
	PendingArmy.clear();
	PendingTitanId = 0;
	TitanList.clear();
	for (const StartTitan& S : C.startTitans)
	{
		TitanState T;
		T.id = NextId++;
		T.def = S.def;
		T.level = std::max(1, std::min(S.level, C.titans[static_cast<size_t>(S.def)].MaxLevel()));
		StartSkills(T);
		TitanList.push_back(T);
	}
	RelicAt = Time;
	RelicCounter = 0;
	ResetSpellsAndRelics();
}

void Kingdom::ResetSpellsAndRelics()
{
	SpellStockList.assign(C.spells.size(), 0);
	SpellLevelList.assign(C.spells.size(), 1);
	Craft.clear();
	CraftStart = Time;
	bCraftBlocked = false;
	SpellLabs.clear();
	PendingSpellList.clear();
	RelicList.clear();
	for (TitanState& T : TitanList)
	{
		T.relics.clear();
	}
	for (Land& L : LandList)
	{
		L.titanId = 0;
	}
}

// --- Levels and checks ------------------------------------------------------------------------

const LevelDef* Kingdom::CurrentLevel(const Building& B) const
{
	const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
	return B.level >= 1 && B.level <= D.MaxLevel() ? &D.levels[static_cast<size_t>(B.level - 1)] : nullptr;
}

const LevelDef* Kingdom::NextLevel(const Building& B) const
{
	const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
	return B.level < D.MaxLevel() ? &D.levels[static_cast<size_t>(B.level)] : nullptr;
}

int Kingdom::CastleLevel() const
{
	int Level = 0;
	for (const Building& B : BuildingList)
	{
		if (B.def == C.castle)
		{
			Level = std::max(Level, B.level);
		}
	}
	return std::max(1, Level);
}

int Kingdom::CountOf(int Def) const
{
	int Count = 0;
	for (const Building& B : BuildingList)
	{
		Count += B.def == Def ? 1 : 0;
	}
	return Count;
}

int Kingdom::MaxCountOf(int Def) const
{
	if (Def < 0 || Def >= static_cast<int>(C.buildings.size()))
	{
		return 0;
	}
	const BuildingDef& D = C.buildings[static_cast<size_t>(Def)];
	return CastleLevel() >= D.unlockCastle ? D.MaxCount(CastleLevel()) : 0;
}

int Kingdom::Builders() const
{
	int64_t Count = 0;
	for (const Building& B : BuildingList)
	{
		if (const LevelDef* L = CurrentLevel(B))
		{
			Count = std::max(Count, L->builders);
		}
	}
	return static_cast<int>(std::max<int64_t>(1, Count));
}

int Kingdom::BusyBuilders() const
{
	int Busy = 0;
	for (const Building& B : BuildingList)
	{
		Busy += B.upgrading ? 1 : 0;
	}
	for (const Land& L : LandList)
	{
		Busy += L.upgrading ? 1 : 0;
	}
	return Busy;
}

int Kingdom::LandCapacity() const
{
	int64_t Base = 0;
	for (const Building& B : BuildingList)
	{
		if (B.def == C.castle)
		{
			if (const LevelDef* L = CurrentLevel(B))
			{
				Base = std::max(Base, L->landCapacity);
			}
		}
	}
	return std::min(C.maxLands, static_cast<int>(Base) + ExtraSlots);
}

int64_t Kingdom::Capacity(Res R) const
{
	if (R == Res::Gems || R == Res::Shards || R == Res::Vp)
	{
		return kUnlimited;
	}
	int64_t Total = 0;
	for (const Building& B : BuildingList)
	{
		const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
		const LevelDef* L = CurrentLevel(B);
		if (!L)
		{
			continue;
		}
		if (R == Res::Stones && D.role == Role::Portal)
		{
			Total += L->stones;
		}
		else if (D.role == Role::Storage && D.resource == R)
		{
			Total += L->capacity;
		}
	}
	return Total;
}

int64_t Kingdom::Protected(Res R) const
{
	int64_t Total = 0;
	for (const Building& B : BuildingList)
	{
		const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
		if (D.role == Role::Reserve && D.resource == R)
		{
			if (const LevelDef* L = CurrentLevel(B))
			{
				Total += L->protect;
			}
		}
	}
	return std::min(Total, Amount(R));
}

int64_t Kingdom::ProducerRate(const Building& B) const
{
	const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
	const LevelDef* L = CurrentLevel(B);
	// An economy relic on the producer adds to its output (the original's pickaxes and ploughs).
	return (D.role == Role::Producer && L && !B.upgrading) ? L->produce * (1000 + BuildingRelicBonus(B, RelicStat::Produce)) / 1000 : 0;
}

int64_t Kingdom::ProducerCap(const Building& B) const
{
	const LevelDef* L = CurrentLevel(B);
	return L ? L->store * (1000 + BuildingRelicBonus(B, RelicStat::Store)) / 1000 : 0;
}

/** Production per period in hundredths of a unit: the titan bonus is a percentage. */
int64_t Kingdom::LandRateNum(const Land& L) const
{
	const LandTypeDef& T = C.landTypes[static_cast<size_t>(L.type)];
	const int64_t Base = T.produce[static_cast<size_t>(std::min<int>(L.level, static_cast<int>(T.produce.size())) - 1)];
	// A titan on the land adds 1% per titan level (the original's rule; stars do not matter).
	const TitanState* Guard = L.titanId ? FindTitan(L.titanId) : nullptr;
	return Base * (100 + (Guard ? Guard->level * C.landTitanPctPerLevel : 0));
}

int64_t Kingdom::LandPeriodSec(const Land& L) const
{
	return C.landTypes[static_cast<size_t>(L.type)].periodHours * kSecondsPerHour;
}

int64_t Kingdom::LandRate(const Land& L) const
{
	return LandRateNum(L) / 100;
}

int64_t Kingdom::LandFillHours(const Land& L) const
{
	const LandTypeDef& T = C.landTypes[static_cast<size_t>(L.type)];
	const std::vector<int64_t>& Fill = T.fillHours.empty() ? C.landFillHours : T.fillHours;
	return Fill[static_cast<size_t>(std::min<int>(L.level, static_cast<int>(Fill.size())) - 1)];
}

int64_t Kingdom::LandStoreCap(const Land& L) const
{
	return LandRateNum(L) * LandFillHours(L) / (100 * C.landTypes[static_cast<size_t>(L.type)].periodHours);
}

int64_t Kingdom::RaidLoot(const WorldLand& W) const
{
	const LandTypeDef& T = C.landTypes[static_cast<size_t>(W.type)];
	const std::vector<int64_t>& Loot = T.raidLoot.empty() ? C.raidLoot : T.raidLoot;
	return Loot[static_cast<size_t>(std::min<int>(W.level, static_cast<int>(Loot.size())) - 1)];
}

int64_t Kingdom::VictoryVp(int LandLevel) const
{
	return C.victoryVp.empty() ? 0 : C.victoryVp[static_cast<size_t>(std::max(1, std::min<int>(LandLevel, static_cast<int>(C.victoryVp.size()))) - 1)];
}

int Kingdom::LandBandLow(int Castle) const
{
	const int MaxLevel = static_cast<int>(C.raidLoot.size());
	return std::max(1, std::min(MaxLevel, (Castle + 1) / 3));
}

int Kingdom::LandBandTop(int Castle) const
{
	const int Lo = LandBandLow(Castle);
	return std::max(Lo, std::min(static_cast<int>(C.raidLoot.size()), Lo + 2));
}

int Kingdom::MaxLandLevel() const
{
	return LandBandTop(CastleLevel());
}

Check Kingdom::CheckLandUpgrade(int32_t Id) const
{
	Check Out;
	const Land* L = FindLand(Id);
	if (!L)
	{
		Out.error = Error::BadId;
		return Out;
	}
	if (L->upgrading)
	{
		Out.error = Error::Busy;
		return Out;
	}
	const LandTypeDef& T = C.landTypes[static_cast<size_t>(L->type)];
	const size_t Next = static_cast<size_t>(L->level); // index of the next level
	// A level that would produce no more is not offered (ruins make one shard a day at every level).
	if (Next >= T.produce.size() || Next >= C.landUpgradeGold.size() || Next >= C.landUpgradeSec.size() || T.produce[Next] <= T.produce[Next - 1])
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	const size_t Gold = static_cast<size_t>(ResIndex(Res::Gold));
	Out.cost[Gold] = C.landUpgradeGold[Next];
	Out.timeSec = C.landUpgradeSec[Next];
	if (L->level + 1 > MaxLandLevel())
	{
		Out.error = Error::CastleTooLow;
		for (int Cl = CastleLevel() + 1; Cl <= C.buildings[static_cast<size_t>(C.castle)].MaxLevel(); ++Cl)
		{
			if (LandBandTop(Cl) > L->level)
			{
				Out.castle = Cl;
				break;
			}
		}
		return Out;
	}
	if (BusyBuilders() >= Builders())
	{
		Out.error = Error::NoBuilder;
		return Out;
	}
	if (Amount(Res::Gold) < Out.cost[Gold])
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[Gold] = Out.cost[Gold] - Amount(Res::Gold);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

int64_t Kingdom::LandSkipCost(int32_t Id) const
{
	const Land* L = FindLand(Id);
	return L && L->upgrading ? GemsForSeconds(std::max<int64_t>(0, L->finishAt - Time)) : 0;
}

int64_t Kingdom::ProductionPerHour(Res R) const
{
	int64_t Total = 0;
	for (const Building& B : BuildingList)
	{
		if (C.buildings[static_cast<size_t>(B.def)].resource == R)
		{
			Total += ProducerRate(B);
		}
	}
	for (const Land& L : LandList)
	{
		const LandTypeDef& T = C.landTypes[static_cast<size_t>(L.type)];
		if (T.resource == R)
		{
			Total += LandRate(L) / T.periodHours;
		}
	}
	return Total;
}

int64_t Kingdom::GemsForResources(const ResArray& Missing) const
{
	int64_t Gems = 0;
	for (int I = 0; I < kResCount; ++I)
	{
		const Res R = static_cast<Res>(I);
		if ((R == Res::Gold || R == Res::Food) && Missing[static_cast<size_t>(I)] > 0)
		{
			Gems += std::max<int64_t>(1, Interpolate(C.gemsForResources, Missing[static_cast<size_t>(I)]));
		}
	}
	return Gems;
}

Check Kingdom::CheckLevel(int Def, const LevelDef& L, bool bNew, bool bNeedsBuilder) const
{
	Check Out;
	Out.cost = L.cost;
	Out.timeSec = L.timeSec;
	const BuildingDef& D = C.buildings[static_cast<size_t>(Def)];
	const int Castle = CastleLevel();
	if (bNew && Castle < D.unlockCastle)
	{
		Out.error = Error::Locked;
		Out.castle = D.unlockCastle;
		return Out;
	}
	if (bNew && CountOf(Def) >= MaxCountOf(Def))
	{
		Out.error = Error::LimitReached;
		// The next Castle level that allows one more, if any.
		for (int Cl = Castle + 1; Cl <= C.buildings[static_cast<size_t>(C.castle)].MaxLevel(); ++Cl)
		{
			if (D.MaxCount(Cl) > CountOf(Def))
			{
				Out.castle = Cl;
				break;
			}
		}
		return Out;
	}
	if (Castle < L.castle)
	{
		Out.error = Error::CastleTooLow;
		Out.castle = L.castle;
		return Out;
	}
	if (L.expansion > ExpansionCount)
	{
		Out.error = Error::NeedExpansion;
		Out.expansion = L.expansion;
		return Out;
	}
	for (const Requirement& Q : L.needs)
	{
		int Have = 0;
		for (const Building& B : BuildingList)
		{
			Have += (B.def == Q.building && B.level >= Q.level) ? 1 : 0;
		}
		if (Have < Q.count)
		{
			Out.error = Error::NeedRequirement;
			Out.missing = Q;
			return Out;
		}
	}
	if (bNeedsBuilder && BusyBuilders() >= Builders())
	{
		Out.error = Error::NoBuilder;
		return Out;
	}
	ResArray Missing{};
	bool bMissing = false;
	for (int I = 0; I < kResCount; ++I)
	{
		Missing[static_cast<size_t>(I)] = std::max<int64_t>(0, L.cost[static_cast<size_t>(I)] - Res_[static_cast<size_t>(I)]);
		bMissing = bMissing || Missing[static_cast<size_t>(I)] > 0;
	}
	if (bMissing)
	{
		Out.error = Error::NotEnough;
		// Gems themselves (and shards) cannot be bought with gems.
		Out.gemsForMissing = OnlyBuyableMissing(Missing) ? GemsForResources(Missing) : -1;
	}
	return Out;
}

Check Kingdom::CheckBuild(int Def) const
{
	if (Def < 0 || Def >= static_cast<int>(C.buildings.size()))
	{
		Check Out;
		Out.error = Error::BadId;
		return Out;
	}
	const BuildingDef& D = C.buildings[static_cast<size_t>(Def)];
	return CheckLevel(Def, D.levels[0], true, !D.instant);
}

Check Kingdom::CheckUpgrade(int32_t Id) const
{
	Check Out;
	const Building* B = Find(Id);
	if (!B)
	{
		Out.error = Error::BadId;
		return Out;
	}
	if (B->upgrading)
	{
		Out.error = Error::Busy;
		return Out;
	}
	const LevelDef* Next = NextLevel(*B);
	if (!Next)
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	return CheckLevel(B->def, *Next, false, !C.buildings[static_cast<size_t>(B->def)].instant);
}

Check Kingdom::CheckExpansion() const
{
	Check Out;
	if (ExpansionCount >= static_cast<int>(C.expansions.size()))
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	const ExpansionDef& E = C.expansions[static_cast<size_t>(ExpansionCount)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Gold))] = E.costGold;
	if (CastleLevel() < E.castle)
	{
		Out.error = Error::CastleTooLow;
		Out.castle = E.castle;
		return Out;
	}
	if (Amount(Res::Gold) < E.costGold)
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Gold))] = E.costGold - Amount(Res::Gold);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

int64_t Kingdom::SkipCost(int32_t Id) const
{
	const Building* B = Find(Id);
	if (!B || !B->upgrading)
	{
		return 0;
	}
	const LevelDef* Next = NextLevel(*B);
	const int64_t Remaining = std::max<int64_t>(0, B->finishAt - Time);
	const int64_t Total = std::max<int64_t>(1, B->finishAt - B->startAt);
	if (!Next || Remaining == 0)
	{
		return 0;
	}
	// Pro rata of the full skip price for the time that is left (at least one gem).
	return std::max<int64_t>(1, CeilDiv(Next->skipGems * Remaining, Total));
}

bool Kingdom::CanPlace(int Def, int X, int Y, int32_t IgnoreId) const
{
	if (Def < 0 || Def >= static_cast<int>(C.buildings.size()))
	{
		return false;
	}
	const BuildingDef& D = C.buildings[static_cast<size_t>(Def)];
	const ExpansionDef& Area = C.expansions[static_cast<size_t>(std::max(0, ExpansionCount - 1))];
	if (X < Area.x0 || Y < Area.y0 || X + D.width > Area.x1 || Y + D.height > Area.y1)
	{
		return false;
	}
	for (const Building& B : BuildingList)
	{
		if (B.id == IgnoreId)
		{
			continue;
		}
		const BuildingDef& O = C.buildings[static_cast<size_t>(B.def)];
		if (X < B.x + O.width && B.x < X + D.width && Y < B.y + O.height && B.y < Y + D.height)
		{
			return false;
		}
	}
	return true;
}

// --- Time ---------------------------------------------------------------------------------------

void Kingdom::Settle(Building& B, int64_t At) const
{
	const int64_t Rate = ProducerRate(B);
	if (Rate > 0 && At > B.lastAt)
	{
		const int64_t Cap = ProducerCap(B);
		const int64_t Gained = Rate * (At - B.lastAt) + B.carry;
		// What was stored stays even above the cap (a relic's extra room may have run out since).
		B.stored = std::max(B.stored, std::min(Cap, B.stored + Gained / kSecondsPerHour));
		B.carry = B.stored >= Cap ? 0 : Gained % kSecondsPerHour;
	}
	B.lastAt = std::max(B.lastAt, At);
}

void Kingdom::SettleLand(Land& L, int64_t At) const
{
	const int64_t Num = LandRateNum(L);
	const int64_t Den = LandPeriodSec(L) * 100;
	if (Num > 0 && At > L.lastAt)
	{
		const int64_t Cap = LandStoreCap(L);
		const int64_t Gained = Num * (At - L.lastAt) + L.carry;
		L.stored = std::min(Cap, L.stored + Gained / Den);
		L.carry = L.stored >= Cap ? 0 : Gained % Den;
	}
	L.lastAt = std::max(L.lastAt, At);
}

int64_t Kingdom::Uncollected(const Building& B) const
{
	Building Copy = B;
	Settle(Copy, Time);
	return Copy.stored;
}

int64_t Kingdom::UncollectedLand(const Land& L) const
{
	Land Copy = L;
	SettleLand(Copy, Time);
	return Copy.stored;
}

void Kingdom::Complete(Building& B, int64_t At)
{
	// Producers do not work while upgrading (settled when the upgrade began).
	B.upgrading = false;
	B.level = std::min(B.level + 1, C.buildings[static_cast<size_t>(B.def)].MaxLevel());
	B.lastAt = At;
	B.carry = 0;
}

void Kingdom::CompleteLand(Land& L, int64_t At)
{
	// A land keeps producing while it is upgraded: at the old level until now, at the new one from now on.
	SettleLand(L, At);
	L.upgrading = false;
	L.level = std::min(L.level + 1, static_cast<int>(C.landTypes[static_cast<size_t>(L.type)].produce.size()));
}

void Kingdom::RegenStones(int64_t Until)
{
	int64_t& Stones = Res_[static_cast<size_t>(ResIndex(Res::Stones))];
	const int64_t Cap = Capacity(Res::Stones);
	if (Stones >= Cap || C.stoneRegenSec <= 0)
	{
		StonesAt = std::max(StonesAt, Until);
		return;
	}
	if (Until <= StonesAt)
	{
		return;
	}
	const int64_t Count = (Until - StonesAt) / C.stoneRegenSec;
	Stones = std::min(Cap, Stones + Count);
	StonesAt += Count * C.stoneRegenSec;
	if (Stones >= Cap)
	{
		StonesAt = Until;
	}
}

int64_t Kingdom::StonesNextAt() const
{
	return Amount(Res::Stones) >= Capacity(Res::Stones) ? 0 : StonesAt + C.stoneRegenSec;
}

void Kingdom::Advance(int64_t Now)
{
	if (Now < Time)
	{
		return; // the clock never runs backwards
	}
	for (;;)
	{
		// Next construction to finish, in time order (ties by id); Armoury research goes first on a tie.
		Building* NextB = nullptr;
		for (Building& B : BuildingList)
		{
			if (B.upgrading && B.finishAt <= Now && (!NextB || B.finishAt < NextB->finishAt || (B.finishAt == NextB->finishAt && B.id < NextB->id)))
			{
				NextB = &B;
			}
		}
		// Land upgrades come after buildings on a tie.
		Land* NextL = nullptr;
		for (Land& L : LandList)
		{
			if (L.upgrading && L.finishAt <= Now && (!NextL || L.finishAt < NextL->finishAt || (L.finishAt == NextL->finishAt && L.id < NextL->id)))
			{
				NextL = &L;
			}
		}
		const bool bResearch = Research >= 0 && ResearchUntil <= Now;
		int Lab = -1;
		for (size_t I = 0; I < SpellLabs.size(); ++I)
		{
			if (SpellLabs[I].until <= Now && (Lab < 0 || SpellLabs[I].until < SpellLabs[static_cast<size_t>(Lab)].until))
			{
				Lab = static_cast<int>(I);
			}
		}
		const int64_t InfT = std::numeric_limits<int64_t>::max();
		const int64_t TB = NextB ? NextB->finishAt : InfT;
		const int64_t TL = NextL ? NextL->finishAt : InfT;
		const int64_t TR = bResearch ? ResearchUntil : InfT;
		const int64_t TS = Lab >= 0 ? SpellLabs[static_cast<size_t>(Lab)].until : InfT;
		// The end of a season is an event too: its league reward may add portal stones.
		const int64_t TN = SeasonOf(Now) > SeasonNo ? std::max(Time, SeasonStart(SeasonNo + 1)) : InfT;
		// So is an economy relic running out: its building works without it from then on.
		RelicItem* Spent = nullptr;
		for (RelicItem& Rl : RelicList)
		{
			if (Rl.building != 0 && Rl.durability > 0 && Rl.since + Rl.durability <= Now && (!Spent || Rl.since + Rl.durability < Spent->since + Spent->durability))
			{
				Spent = &Rl;
			}
		}
		const int64_t TX = Spent ? Spent->since + Spent->durability : InfT;
		// Raids come last on a tie, and only until the player has been away too long.
		const int64_t TA = BattleContent && NextRaid <= Now && NextRaid <= VisitAt + C.raids.awaySec ? std::max(Time, NextRaid) : InfT;
		if (TB == InfT && TL == InfT && TR == InfT && TS == InfT && TN == InfT && TX == InfT && TA == InfT)
		{
			break;
		}
		const int64_t At = std::min(std::min(std::min(TB, TL), std::min(TR, TS)), std::min(std::min(TN, TX), TA));
		ProcessQueue(At);
		ProcessCraft(At);
		RegenStones(At);
		if (TX == At && TB != At && TL != At && TR != At && TS != At && TN != At)
		{
			for (Building& B : BuildingList)
			{
				if (B.id == Spent->building)
				{
					Settle(B, At);
				}
			}
			Spent->durability = 0;
			Spent->since = At;
		}
		else if (TN == At)
		{
			RollSeason(At);
		}
		else if (TR == At)
		{
			TroopLevels[static_cast<size_t>(Research)] = std::min(TroopLevels[static_cast<size_t>(Research)] + 1, C.troops[static_cast<size_t>(Research)].MaxLevel());
			Research = -1;
		}
		else if (TS == At)
		{
			int& Level = SpellLevelList[static_cast<size_t>(SpellLabs[static_cast<size_t>(Lab)].spell)];
			Level = std::min(Level + 1, std::max(Level, MaxSpellLevel()));
			SpellLabs.erase(SpellLabs.begin() + Lab);
		}
		else if (TB == At)
		{
			Complete(*NextB, At);
			UnblockQueue(At); // a finished Army Camp may have room now
			UnblockCraft(At); // and a finished Spell Forge
		}
		else if (TL == At)
		{
			CompleteLand(*NextL, At);
		}
		else
		{
			RunRaid(At);
		}
	}
	ProcessQueue(Now);
	ProcessCraft(Now);
	RegenStones(Now);
	Time = Now;
	RollQuests();
}

// --- Actions ------------------------------------------------------------------------------------

bool Kingdom::Pay(const ResArray& Cost, bool UseGems)
{
	ResArray Missing{};
	bool bMissing = false;
	for (int I = 0; I < kResCount; ++I)
	{
		Missing[static_cast<size_t>(I)] = std::max<int64_t>(0, Cost[static_cast<size_t>(I)] - Res_[static_cast<size_t>(I)]);
		bMissing = bMissing || Missing[static_cast<size_t>(I)] > 0;
	}
	const size_t Gem = static_cast<size_t>(ResIndex(Res::Gems));
	if (bMissing)
	{
		if (!UseGems || !OnlyBuyableMissing(Missing))
		{
			return false;
		}
		const int64_t Gems = GemsForResources(Missing);
		if (Res_[Gem] < Gems + Cost[Gem])
		{
			return false;
		}
		Res_[Gem] -= Gems;
	}
	for (int I = 0; I < kResCount; ++I)
	{
		Res_[static_cast<size_t>(I)] = std::max<int64_t>(0, Res_[static_cast<size_t>(I)] - Cost[static_cast<size_t>(I)]);
	}
	return true;
}

Error Kingdom::Build(int Def, int X, int Y, bool UseGems, int32_t* OutId)
{
	const Check Ck = CheckBuild(Def);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!CanPlace(Def, X, Y, 0))
	{
		return Error::BadPlacement;
	}
	const BuildingDef& D = C.buildings[static_cast<size_t>(Def)];
	const LevelDef& L = D.levels[0];
	if (!Pay(L.cost, UseGems))
	{
		return Error::NotEnough;
	}
	Building B;
	B.id = NextId++;
	B.def = Def;
	B.level = 0;
	B.x = X;
	B.y = Y;
	B.lastAt = Time;
	B.upgrading = true;
	B.startAt = Time;
	B.finishAt = Time + L.timeSec;
	BuildingList.push_back(B);
	if (L.timeSec <= 0)
	{
		Complete(BuildingList.back(), Time);
	}
	if (OutId)
	{
		*OutId = B.id;
	}
	NoteQuest(QuestEvent::Upgrade, 1);
	return Error::None;
}

Error Kingdom::Upgrade(int32_t Id, bool UseGems)
{
	const Check Ck = CheckUpgrade(Id);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	Building* B = nullptr;
	for (Building& It : BuildingList)
	{
		if (It.id == Id)
		{
			B = &It;
		}
	}
	const LevelDef* Next = NextLevel(*B);
	if (!Pay(Next->cost, UseGems))
	{
		return Error::NotEnough;
	}
	Settle(*B, Time);
	B->upgrading = true;
	B->startAt = Time;
	B->finishAt = Time + Next->timeSec;
	if (Next->timeSec <= 0)
	{
		Complete(*B, Time);
	}
	NoteQuest(QuestEvent::Upgrade, 1);
	return Error::None;
}

Error Kingdom::Speedup(int32_t Id)
{
	Building* B = nullptr;
	for (Building& It : BuildingList)
	{
		if (It.id == Id)
		{
			B = &It;
		}
	}
	if (!B)
	{
		return Error::BadId;
	}
	if (!B->upgrading)
	{
		return Error::NotBusy;
	}
	const int64_t Gems = SkipCost(Id);
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	if (Have < Gems)
	{
		return Error::NotEnough;
	}
	Have -= Gems;
	RegenStones(Time);
	Complete(*B, Time);
	UnblockQueue(Time);
	return Error::None;
}

void Kingdom::AddResource(Res R, int64_t Amount)
{
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(R))];
	Have = std::min(std::max<int64_t>(Have, Capacity(R)), Have + Amount);
}

Error Kingdom::Collect(int32_t Id, int64_t* OutAmount)
{
	Building* B = nullptr;
	for (Building& It : BuildingList)
	{
		if (It.id == Id)
		{
			B = &It;
		}
	}
	if (!B)
	{
		return Error::BadId;
	}
	const BuildingDef& D = C.buildings[static_cast<size_t>(B->def)];
	if (D.role != Role::Producer)
	{
		return Error::Nothing;
	}
	Settle(*B, Time);
	const int64_t Space = std::max<int64_t>(0, Capacity(D.resource) - Amount(D.resource));
	const int64_t Take = std::min(B->stored, Space);
	if (OutAmount)
	{
		*OutAmount = Take;
	}
	if (B->stored <= 0)
	{
		return Error::Nothing;
	}
	if (Take <= 0)
	{
		return Error::StorageFull;
	}
	const bool bWasFull = B->stored >= ProducerCap(*B);
	B->stored -= Take;
	Res_[static_cast<size_t>(ResIndex(D.resource))] += Take;
	NoteQuest(D.resource == Res::Gold ? QuestEvent::CollectGold : D.resource == Res::Food ? QuestEvent::CollectFood : QuestEvent::Count, Take);
	if (bWasFull)
	{
		B->carry = 0;
	}
	return Error::None;
}

Error Kingdom::CollectAll(ResArray* OutAmounts)
{
	ResArray Got{};
	bool bAny = false;
	bool bFull = false;
	for (Building& B : BuildingList)
	{
		if (C.buildings[static_cast<size_t>(B.def)].role != Role::Producer)
		{
			continue;
		}
		int64_t Amount = 0;
		const Error E = Collect(B.id, &Amount);
		bAny = bAny || E == Error::None;
		bFull = bFull || E == Error::StorageFull;
		Got[static_cast<size_t>(ResIndex(C.buildings[static_cast<size_t>(B.def)].resource))] += Amount;
	}
	for (Land& L : LandList)
	{
		int64_t Amount = 0;
		const Error E = CollectLand(L.id, &Amount);
		bAny = bAny || E == Error::None;
		bFull = bFull || E == Error::StorageFull;
		Got[static_cast<size_t>(ResIndex(C.landTypes[static_cast<size_t>(L.type)].resource))] += Amount;
	}
	if (OutAmounts)
	{
		*OutAmounts = Got;
	}
	return bAny ? Error::None : (bFull ? Error::StorageFull : Error::Nothing);
}

Error Kingdom::Move(int32_t Id, int X, int Y)
{
	for (Building& B : BuildingList)
	{
		if (B.id == Id)
		{
			if (!CanPlace(B.def, X, Y, Id))
			{
				return Error::BadPlacement;
			}
			B.x = X;
			B.y = Y;
			return Error::None;
		}
	}
	return Error::BadId;
}

Error Kingdom::BuyExpansion(bool UseGems)
{
	const Check Ck = CheckExpansion();
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!Pay(Ck.cost, UseGems))
	{
		return Error::NotEnough;
	}
	++ExpansionCount;
	return Error::None;
}

Error Kingdom::BuyLandSlot()
{
	if (ExtraSlots >= static_cast<int>(C.extraSlotGems.size()) || LandCapacity() >= C.maxLands)
	{
		return Error::MaxLevel;
	}
	int64_t& Gems = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	const int64_t Price = C.extraSlotGems[static_cast<size_t>(ExtraSlots)];
	if (Gems < Price)
	{
		return Error::NotEnough;
	}
	Gems -= Price;
	++ExtraSlots;
	return Error::None;
}

Error Kingdom::RefillStones()
{
	const int64_t Missing = Capacity(Res::Stones) - Amount(Res::Stones);
	if (Missing <= 0)
	{
		return Error::Nothing;
	}
	int64_t& Gems = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	const int64_t Price = Missing * C.gemsPerStone;
	if (Gems < Price)
	{
		return Error::NotEnough;
	}
	Gems -= Price;
	Res_[static_cast<size_t>(ResIndex(Res::Stones))] += Missing;
	StonesAt = Time;
	return Error::None;
}

Error Kingdom::CollectLand(int32_t Id, int64_t* OutAmount)
{
	for (Land& L : LandList)
	{
		if (L.id != Id)
		{
			continue;
		}
		SettleLand(L, Time);
		const Res R = C.landTypes[static_cast<size_t>(L.type)].resource;
		const int64_t Space = std::max<int64_t>(0, Capacity(R) - Amount(R));
		const int64_t Take = std::min(L.stored, Space);
		if (OutAmount)
		{
			*OutAmount = Take;
		}
		if (L.stored <= 0)
		{
			return Error::Nothing;
		}
		if (Take <= 0)
		{
			return Error::StorageFull;
		}
		const bool bWasFull = L.stored >= LandStoreCap(L);
		L.stored -= Take;
		Res_[static_cast<size_t>(ResIndex(R))] += Take;
		NoteQuest(R == Res::Gold ? QuestEvent::CollectGold : R == Res::Food ? QuestEvent::CollectFood : QuestEvent::Count, Take);
		if (bWasFull)
		{
			L.carry = 0;
		}
		return Error::None;
	}
	return Error::BadId;
}

Error Kingdom::AbandonLand(int32_t Id)
{
	for (size_t I = 0; I < LandList.size(); ++I)
	{
		if (LandList[I].id == Id)
		{
			if (LandList[I].upgrading)
			{
				return Error::Busy; // the gold for the upgrade would be lost
			}
			LandList.erase(LandList.begin() + static_cast<std::ptrdiff_t>(I));
			return Error::None;
		}
	}
	return Error::BadId;
}

Error Kingdom::UpgradeLand(int32_t Id, bool UseGems)
{
	const Check Ck = CheckLandUpgrade(Id);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!Pay(Ck.cost, UseGems))
	{
		return Error::NotEnough;
	}
	for (Land& L : LandList)
	{
		if (L.id == Id)
		{
			SettleLand(L, Time);
			L.upgrading = true;
			L.startAt = Time;
			L.finishAt = Time + Ck.timeSec;
			if (Ck.timeSec <= 0)
			{
				CompleteLand(L, Time);
			}
		}
	}
	NoteQuest(QuestEvent::Upgrade, 1);
	return Error::None;
}

Error Kingdom::SpeedupLand(int32_t Id)
{
	for (Land& L : LandList)
	{
		if (L.id != Id)
		{
			continue;
		}
		if (!L.upgrading)
		{
			return Error::NotBusy;
		}
		const int64_t Gems = LandSkipCost(Id);
		int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
		if (Have < Gems)
		{
			return Error::NotEnough;
		}
		Have -= Gems;
		CompleteLand(L, Time);
		return Error::None;
	}
	return Error::BadId;
}

// --- Seasons ------------------------------------------------------------------------------------

int64_t Kingdom::SeasonOf(int64_t T) const
{
	return FloorDiv(T - C.seasonEpoch, C.seasonSec);
}

int64_t Kingdom::SeasonStart(int64_t Season) const
{
	return C.seasonEpoch + Season * C.seasonSec;
}

int Kingdom::League(int64_t Vp) const
{
	int Best = -1;
	for (size_t I = 0; I < C.leagues.size(); ++I)
	{
		Best = Vp >= C.leagues[I].vp ? static_cast<int>(I) : Best;
	}
	return Best;
}

void Kingdom::RollSeason(int64_t At)
{
	(void)At; // stones were regenerated up to At by the caller
	int64_t& Vp = Res_[static_cast<size_t>(ResIndex(Res::Vp))];
	// Every alliance league is ranked and moves; a member is paid by the alliance's league (the original's leagues
	// are alliance leagues). Alone, or new to the alliance, the personal league pays instead.
	const bool bAlliancePaid = RollAllianceLeagues(SeasonStart(SeasonNo + 1));
	if (!bAlliancePaid && Vp > 0)
	{
		Last = SeasonResult();
		Last.season = SeasonNo;
		Last.vp = Vp;
		Last.league = League(Vp);
		if (Last.league >= 0)
		{
			const LeagueDef& L = C.leagues[static_cast<size_t>(Last.league)];
			Res_[static_cast<size_t>(ResIndex(Res::Gems))] += L.gems;
			const int64_t Before = Amount(Res::Stones);
			AddResource(Res::Stones, L.stones);
			Last.gems = L.gems;
			Last.stones = Amount(Res::Stones) - Before;
		}
	}
	// Everyone's VP returns to zero for the new season (wiki, Leagues).
	Vp = 0;
	++SeasonNo;
}

// --- Alliances ----------------------------------------------------------------------------------
// The other alliances and their members are computer players (there is no server yet): who they are, how strong and
// how many follow from the kingdom's seed; their VP grow through the week at their own pace. Only the leagues the
// alliances are in change, once a week, and they are saved.

namespace
{
uint64_t AllyHash(uint64_t Seed, uint64_t A, uint64_t B, uint64_t Cc)
{
	return Mix(Mix(Mix(Seed ^ 0x616c6c69616e6365ULL, A), B), Cc);
}

/** Trims spaces; true when the text is a usable alliance name or tag (UTF-8, no control characters). */
bool CleanName(const std::string& In, size_t MaxBytes, std::string& Out)
{
	size_t B = 0;
	size_t E = In.size();
	while (B < E && In[B] == ' ')
	{
		++B;
	}
	while (E > B && In[E - 1] == ' ')
	{
		--E;
	}
	Out = In.substr(B, E - B);
	for (const char Ch : Out)
	{
		if (static_cast<unsigned char>(Ch) < 0x20 || Ch == 0x7f)
		{
			return false;
		}
	}
	return !Out.empty() && Out.size() <= MaxBytes;
}

const size_t kMaxNameBytes = 48; // 24 Cyrillic letters
const size_t kMaxTagBytes = 10; // 5 Cyrillic letters
} // namespace

void Kingdom::ResetAlliances()
{
	const int N = BaseAlliances();
	const int Leagues = static_cast<int>(C.alliances.leagues.size());
	// Which name each alliance gets: a shuffle by the seed, so every kingdom has its own neighbours.
	AlliancePerm.resize(static_cast<size_t>(N));
	for (int I = 0; I < N; ++I)
	{
		AlliancePerm[static_cast<size_t>(I)] = I;
	}
	for (int I = N - 1; I > 0; --I)
	{
		const int J = static_cast<int>(Mix(Seed ^ 0x6e616d6573ULL, static_cast<uint64_t>(I)) % static_cast<uint64_t>(I + 1));
		std::swap(AlliancePerm[static_cast<size_t>(I)], AlliancePerm[static_cast<size_t>(J)]);
	}
	// At the start every league holds groupSize alliances, the strongest in the top one.
	AllianceTiers.assign(static_cast<size_t>(N), 0);
	for (int I = 0; I < N; ++I)
	{
		AllianceTiers[static_cast<size_t>(I)] = Leagues > 0 ? std::min(I / C.alliances.groupSize, Leagues - 1) : 0;
	}
	AllianceIdx = -1;
	AllianceJoinedAt = 0;
	OwnName.clear();
	OwnTag.clear();
	OwnFounded = 0;
	ReinforceList.clear();
	ReinforceAt = 0;
	bReinforceAsked = false;
	PendingReinforce.clear();
}

int Kingdom::AllianceCount() const
{
	return BaseAlliances() + (HasOwnAlliance() ? 1 : 0);
}

int Kingdom::AllianceTier(int Index) const
{
	return Index >= 0 && Index < static_cast<int>(AllianceTiers.size()) ? AllianceTiers[static_cast<size_t>(Index)] : 0;
}

int64_t Kingdom::AllianceStrength(int Index) const
{
	const std::vector<AllianceLeagueDef>& Leagues = C.alliances.leagues;
	if (Leagues.empty())
	{
		return 0;
	}
	if (Index >= BaseAlliances())
	{
		// Recruits of an own alliance play like the weakest of its league.
		return Leagues[static_cast<size_t>(std::max(0, std::min(AllianceTier(Index), static_cast<int>(Leagues.size()) - 1)))].memberVpMin;
	}
	// A computer alliance keeps the strength of the league it started in, wherever it goes.
	const AllianceLeagueDef& L = Leagues[static_cast<size_t>(std::min(Index / C.alliances.groupSize, static_cast<int>(Leagues.size()) - 1))];
	const uint64_t H = AllyHash(Seed, static_cast<uint64_t>(Index), 1, 0);
	return L.memberVpMin + (L.memberVpMax - L.memberVpMin) * static_cast<int64_t>(H % 1001) / 1000;
}

int Kingdom::RecruitsAt(int64_t At) const
{
	if (!HasOwnAlliance() || At <= OwnFounded)
	{
		return 0;
	}
	return static_cast<int>(std::min<int64_t>(C.alliances.recruits, (At - OwnFounded) / C.alliances.recruitSec));
}

int Kingdom::AllianceSize(int Index) const
{
	if (Index >= BaseAlliances())
	{
		return RecruitsAt(Time);
	}
	const AllianceRules& A = C.alliances;
	return A.minMembers + static_cast<int>(AllyHash(Seed, static_cast<uint64_t>(Index), 2, 0) % static_cast<uint64_t>(A.maxMembers - A.minMembers + 1));
}

int64_t Kingdom::AllianceJoinVp(int Index) const
{
	const AllianceRules& A = C.alliances;
	if (Index >= BaseAlliances())
	{
		return A.joinVp;
	}
	// Strong alliances ask more: one and a half weeks of their average member, rounded down to 500.
	return std::max(A.joinVp, std::min(A.joinVpMax, AllianceStrength(Index) * 3 / 2 / 500 * 500));
}

int64_t Kingdom::MemberVp(int Index, int Member, int64_t Season, int64_t At) const
{
	const bool bOwn = Index >= BaseAlliances();
	const uint64_t Key = bOwn ? 0x100000000ULL + static_cast<uint64_t>(OwnFounded) : static_cast<uint64_t>(Index);
	const uint64_t H = AllyHash(Seed, Key, 3 + static_cast<uint64_t>(Member), static_cast<uint64_t>(Season));
	// A member's pace this week: 50-150% of the alliance's strength, starting up to a quarter of the week late.
	const int64_t Weekly = AllianceStrength(Index) * (50 + static_cast<int64_t>(H % 101)) / 100;
	const int64_t Len = C.seasonSec;
	const int64_t Start = SeasonStart(Season);
	int64_t From = Start + static_cast<int64_t>((H >> 20) % 251) * Len / 1000;
	if (bOwn)
	{
		From = std::max(From, OwnFounded + (Member + 1) * C.alliances.recruitSec);
	}
	const int64_t T = std::min(At, Start + Len);
	return T > From ? Weekly * (T - From) / Len : 0;
}

int64_t Kingdom::AllianceVp(int Index, int64_t Season, int64_t At) const
{
	const int Size = Index >= BaseAlliances() ? RecruitsAt(std::min(At, SeasonStart(Season + 1))) : AllianceSize(Index);
	int64_t Total = 0;
	for (int M = 0; M < Size; ++M)
	{
		Total += MemberVp(Index, M, Season, At);
	}
	// The player's VP count for the alliance too (the original).
	if (Index == AllianceIdx && Season == SeasonNo)
	{
		Total += Amount(Res::Vp);
	}
	return Total;
}

AllianceInfo Kingdom::Alliance(int Index) const
{
	AllianceInfo A;
	if (Index < 0 || Index >= AllianceCount())
	{
		return A;
	}
	A.index = Index;
	A.own = Index >= BaseAlliances();
	if (A.own)
	{
		A.name = OwnName;
		A.tag = OwnTag;
		A.emblem = static_cast<int>(AllyHash(Seed, 0x100000000ULL + static_cast<uint64_t>(OwnFounded), 0, 0) % 12);
	}
	else
	{
		const std::pair<std::string, std::string>& N = C.alliances.names[static_cast<size_t>(AlliancePerm[static_cast<size_t>(Index)])];
		A.name = N.first;
		A.tag = N.second;
		A.emblem = static_cast<int>(AllyHash(Seed, static_cast<uint64_t>(Index), 0, 0) % 12);
	}
	A.tier = AllianceTier(Index);
	A.members = AllianceSize(Index) + (AllianceIdx == Index ? 1 : 0);
	A.joinVp = AllianceJoinVp(Index);
	A.vp = AllianceVp(Index, SeasonNo, Time);
	return A;
}

int64_t Kingdom::AllianceRating() const
{
	return std::max(Amount(Res::Vp), Last.season == SeasonNo - 1 ? Last.vp : 0);
}

std::vector<int> Kingdom::LeagueTable(int Tier) const
{
	std::vector<std::pair<int64_t, int>> Rows;
	for (int I = 0; I < AllianceCount(); ++I)
	{
		if (AllianceTier(I) == Tier)
		{
			Rows.emplace_back(AllianceVp(I, SeasonNo, Time), I);
		}
	}
	std::sort(Rows.begin(), Rows.end(), [](const std::pair<int64_t, int>& X, const std::pair<int64_t, int>& Y)
		{ return X.first != Y.first ? X.first > Y.first : X.second < Y.second; });
	std::vector<int> Out;
	for (const std::pair<int64_t, int>& R : Rows)
	{
		Out.push_back(R.second);
	}
	return Out;
}

std::vector<AllianceMember> Kingdom::Members(int Index) const
{
	std::vector<AllianceMember> Out;
	if (Index < 0 || Index >= AllianceCount())
	{
		return Out;
	}
	const bool bOwn = Index >= BaseAlliances();
	const std::vector<std::string>& Names = C.alliances.memberNames;
	const uint64_t NameCount = static_cast<uint64_t>(Names.size());
	const uint64_t Key = bOwn ? 0x100000000ULL + static_cast<uint64_t>(OwnFounded) : static_cast<uint64_t>(Index);
	const uint64_t H = AllyHash(Seed, Key, 0, 1);
	// Names walk the list with a step coprime to its (prime) length, so they never repeat within an alliance.
	const uint64_t First = H % NameCount;
	const uint64_t Step = NameCount > 1 ? 1 + (H >> 32) % (NameCount - 1) : 1;
	if (bOwn)
	{
		AllianceMember You;
		You.role = 0;
		You.vp = Amount(Res::Vp);
		You.joinedAt = OwnFounded;
		Out.push_back(You);
	}
	const int Size = AllianceSize(Index);
	for (int M = 0; M < Size; ++M)
	{
		AllianceMember A;
		A.name = Names[static_cast<size_t>((First + static_cast<uint64_t>(M) * Step) % NameCount)];
		A.role = bOwn ? 2 : (M == 0 ? 0 : (M <= 2 ? 1 : 2));
		A.vp = MemberVp(Index, M, SeasonNo, Time);
		A.joinedAt = bOwn ? OwnFounded + (M + 1) * C.alliances.recruitSec : 0;
		Out.push_back(A);
	}
	if (!bOwn && AllianceIdx == Index)
	{
		AllianceMember You;
		You.vp = Amount(Res::Vp);
		You.joinedAt = AllianceJoinedAt;
		Out.push_back(You);
	}
	return Out;
}

SeasonResult Kingdom::LeagueReward(int Tier, int Place, int GroupSize) const
{
	SeasonResult R;
	const std::vector<AllianceLeagueDef>& Leagues = C.alliances.leagues;
	if (Tier < 0 || Tier >= static_cast<int>(Leagues.size()) || Place < 1)
	{
		return R;
	}
	const AllianceLeagueDef& L = Leagues[static_cast<size_t>(Tier)];
	const int64_t Pct = std::max<int64_t>(C.alliances.minPlacePct, 100 - static_cast<int64_t>(C.alliances.placeStepPct) * (Place - 1));
	R.allianceTier = Tier;
	R.place = Place;
	R.groupSize = GroupSize;
	R.gems = L.gems * Pct / 100;
	R.stones = L.stones > 0 ? std::max<int64_t>(1, L.stones * Pct / 100) : 0;
	R.crystal = L.crystal;
	return R;
}

int Kingdom::TitanChancePct(int Tier, int Place, int GroupSize) const
{
	const std::vector<AllianceLeagueDef>& Leagues = C.alliances.leagues;
	if (Tier < 0 || Tier >= static_cast<int>(Leagues.size()))
	{
		return 0;
	}
	const AllianceLeagueDef& L = Leagues[static_cast<size_t>(Tier)];
	const int Span = std::max(1, GroupSize - 1);
	const int P = std::max(0, std::min(Span, Place - 1));
	return L.titanPctFirst + (L.titanPctLast - L.titanPctFirst) * P / Span;
}

bool Kingdom::RollAllianceLeagues(int64_t End)
{
	const int N = AllianceCount();
	const int Tiers = static_cast<int>(C.alliances.leagues.size());
	if (N == 0 || Tiers == 0)
	{
		return false;
	}
	std::vector<std::vector<std::pair<int64_t, int>>> Table(static_cast<size_t>(Tiers));
	for (int I = 0; I < N; ++I)
	{
		const int T = std::max(0, std::min(Tiers - 1, AllianceTier(I)));
		Table[static_cast<size_t>(T)].emplace_back(AllianceVp(I, SeasonNo, End), I);
	}
	for (std::vector<std::pair<int64_t, int>>& Rows : Table)
	{
		std::sort(Rows.begin(), Rows.end(), [](const std::pair<int64_t, int>& X, const std::pair<int64_t, int>& Y)
			{ return X.first != Y.first ? X.first > Y.first : X.second < Y.second; });
	}
	// The player's alliance pays every member who was in it for long enough before the end.
	bool bPaid = false;
	if (AllianceIdx >= 0 && AllianceJoinedAt <= End - C.alliances.memberSec)
	{
		const int T = std::max(0, std::min(Tiers - 1, AllianceTier(AllianceIdx)));
		const std::vector<std::pair<int64_t, int>>& Rows = Table[static_cast<size_t>(T)];
		int Place = 1;
		int64_t Total = 0;
		for (size_t K = 0; K < Rows.size(); ++K)
		{
			if (Rows[K].second == AllianceIdx)
			{
				Place = static_cast<int>(K) + 1;
				Total = Rows[K].first;
			}
		}
		const int Size = static_cast<int>(Rows.size());
		const int64_t Vp = Amount(Res::Vp);
		Last = LeagueReward(T, Place, Size);
		Last.season = SeasonNo;
		Last.vp = Vp;
		Last.league = League(Vp);
		Last.alliance = AllianceIdx;
		Last.allianceVp = Total;
		Res_[static_cast<size_t>(ResIndex(Res::Gems))] += Last.gems;
		const int64_t Before = Amount(Res::Stones);
		AddResource(Res::Stones, Last.stones);
		Last.stones = Amount(Res::Stones) - Before;
		// The crystal: a titan by chance (better at a higher place) while the Hall has room, else a relic of its
		// rarity, else (the inventory is full) gems.
		if (Last.crystal > 0)
		{
			const uint64_t H = Mix(Seed ^ 0x6372797374616cULL, static_cast<uint64_t>(SeasonNo));
			if (static_cast<int>(H % 100) < TitanChancePct(T, Place, Size) && static_cast<int>(TitanList.size()) < TitanCapacity())
			{
				Last.crystalTitan = SummonTitan(Last.crystal, H >> 24);
			}
			else
			{
				Last.crystalRelic = AddRelic(RollRelicKind(H >> 32), Last.crystal);
				if (Last.crystalRelic == 0)
				{
					const std::vector<int64_t>& G = C.alliances.crystalGems;
					Last.crystalGems = G.empty() ? 0 : G[static_cast<size_t>(std::min<int>(Last.crystal, static_cast<int>(G.size())) - 1)];
					Res_[static_cast<size_t>(ResIndex(Res::Gems))] += Last.crystalGems;
				}
			}
		}
		bPaid = true;
	}
	// The best of each league go up and as many of the worst of the league above come down.
	std::vector<int> NewTiers = AllianceTiers;
	NewTiers.resize(static_cast<size_t>(N), 0);
	for (int T = 0; T + 1 < Tiers; ++T)
	{
		const std::vector<std::pair<int64_t, int>>& Low = Table[static_cast<size_t>(T)];
		const std::vector<std::pair<int64_t, int>>& High = Table[static_cast<size_t>(T + 1)];
		const int Moves = std::min(C.alliances.promote, static_cast<int>(std::min(Low.size(), High.size()) / 2));
		for (int K = 0; K < Moves; ++K)
		{
			NewTiers[static_cast<size_t>(Low[static_cast<size_t>(K)].second)] = T + 1;
			NewTiers[static_cast<size_t>(High[High.size() - 1 - static_cast<size_t>(K)].second)] = T;
		}
	}
	if (bPaid)
	{
		Last.move = NewTiers[static_cast<size_t>(AllianceIdx)] - AllianceTier(AllianceIdx);
	}
	AllianceTiers = NewTiers;
	return bPaid;
}

Check Kingdom::CheckJoin(int Index) const
{
	Check Out;
	if (AllianceIdx >= 0)
	{
		Out.error = Error::InAlliance;
		return Out;
	}
	if (Index < 0 || Index >= BaseAlliances())
	{
		Out.error = Error::BadId;
		return Out;
	}
	Out.cost[static_cast<size_t>(ResIndex(Res::Vp))] = AllianceJoinVp(Index);
	if (AllianceRating() < AllianceJoinVp(Index))
	{
		Out.error = Error::NeedVp;
	}
	else if (AllianceSize(Index) >= C.alliances.maxMembers)
	{
		Out.error = Error::AllianceFull;
	}
	return Out;
}

Check Kingdom::CheckCreate() const
{
	Check Out;
	Out.cost[static_cast<size_t>(ResIndex(Res::Gems))] = C.alliances.createGems;
	if (AllianceIdx >= 0)
	{
		Out.error = Error::InAlliance;
	}
	else if (C.alliances.leagues.empty())
	{
		Out.error = Error::Locked;
	}
	else if (AllianceRating() < C.alliances.joinVp)
	{
		Out.error = Error::NeedVp;
		Out.cost[static_cast<size_t>(ResIndex(Res::Vp))] = C.alliances.joinVp;
	}
	else if (Amount(Res::Gems) < C.alliances.createGems)
	{
		Out.error = Error::NotEnough;
	}
	return Out;
}

Error Kingdom::JoinAlliance(int Index)
{
	const Check Ck = CheckJoin(Index);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	AllianceIdx = Index;
	AllianceJoinedAt = Time;
	ReinforceList.clear();
	return Error::None;
}

Error Kingdom::CreateAlliance(const std::string& Name, const std::string& Tag)
{
	const Check Ck = CheckCreate();
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	std::string CleanTitle;
	std::string CleanTag;
	if (!CleanName(Name, kMaxNameBytes, CleanTitle) || !CleanName(Tag, kMaxTagBytes, CleanTag))
	{
		return Error::BadId;
	}
	Res_[static_cast<size_t>(ResIndex(Res::Gems))] -= C.alliances.createGems;
	OwnName = CleanTitle;
	OwnTag = CleanTag;
	OwnFounded = Time;
	AllianceTiers.resize(static_cast<size_t>(BaseAlliances()), 0);
	AllianceTiers.push_back(0); // a new alliance starts in the lowest league
	AllianceIdx = BaseAlliances();
	AllianceJoinedAt = Time;
	ReinforceList.clear();
	return Error::None;
}

Error Kingdom::LeaveAlliance()
{
	if (AllianceIdx < 0)
	{
		return Error::NoAlliance;
	}
	if (HasOwnAlliance())
	{
		// An own alliance is disbanded: its recruits leave too.
		AllianceTiers.resize(static_cast<size_t>(BaseAlliances()));
		OwnName.clear();
		OwnTag.clear();
		OwnFounded = 0;
	}
	AllianceIdx = -1;
	AllianceJoinedAt = 0;
	ReinforceList.clear();
	return Error::None;
}

int Kingdom::ReinforceCapacity() const
{
	const LevelDef* L = C.allianceCamp >= 0 ? BuildingLevel(C.allianceCamp) : nullptr;
	return L ? static_cast<int>(L->reinforcements) : 0;
}

int Kingdom::ReinforcementsReady() const
{
	int N = 0;
	for (const Reinforcement& R : ReinforceList)
	{
		N += R.at <= Time ? 1 : 0;
	}
	return N;
}

int64_t Kingdom::ReinforceReadyAt() const
{
	return bReinforceAsked ? ReinforceAt + C.alliances.reinforceCooldownSec : 0;
}

Check Kingdom::CheckReinforce() const
{
	Check Out;
	if (AllianceIdx < 0)
	{
		Out.error = Error::NoAlliance;
	}
	else if (ReinforceCapacity() <= 0)
	{
		Out.error = Error::Locked;
		Out.castle = C.allianceCamp >= 0 ? C.buildings[static_cast<size_t>(C.allianceCamp)].unlockCastle : 0;
	}
	else if (static_cast<int>(ReinforceList.size()) >= ReinforceCapacity())
	{
		Out.error = Error::StorageFull;
	}
	else if (ReinforceReadyAt() > Time)
	{
		Out.error = Error::NotReady;
		Out.timeSec = ReinforceReadyAt() - Time;
	}
	else if (AllianceSize(AllianceIdx) <= 0)
	{
		Out.error = Error::Nothing; // an own alliance with no recruits yet
	}
	return Out;
}

Error Kingdom::RequestReinforcements()
{
	const Check Ck = CheckReinforce();
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	// Allies send troops a player of this Castle could have, at their league's level but no stronger than the
	// player's Armoury allows; one squad a minute until the camp is full.
	std::vector<int> Pool;
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		if (C.troops[I].unlockCastle <= CastleLevel())
		{
			Pool.push_back(static_cast<int>(I));
		}
	}
	if (Pool.empty())
	{
		return Error::Nothing;
	}
	const std::vector<AllianceLeagueDef>& Leagues = C.alliances.leagues;
	const int Tier = std::max(0, std::min(static_cast<int>(Leagues.size()) - 1, AllianceTier(AllianceIdx)));
	const int Level = std::max(1, std::min(Leagues[static_cast<size_t>(Tier)].troopLevel, MaxTroopLevel()));
	const int Senders = AllianceSize(AllianceIdx);
	const int Free = ReinforceCapacity() - static_cast<int>(ReinforceList.size());
	int64_t At = Time;
	for (const Reinforcement& R : ReinforceList)
	{
		At = std::max(At, R.at);
	}
	for (int K = 0; K < Free; ++K)
	{
		const uint64_t H = Mix(Mix(Seed ^ 0x7265696e66ULL, static_cast<uint64_t>(Time)), static_cast<uint64_t>(K));
		Reinforcement R;
		R.troop = Pool[static_cast<size_t>(H % static_cast<uint64_t>(Pool.size()))];
		R.level = std::min(Level, C.troops[static_cast<size_t>(R.troop)].MaxLevel());
		At += C.alliances.reinforceArriveSec;
		R.at = At;
		// Members() lists the player first in an own alliance, last in a computer one.
		R.from = static_cast<int>((H >> 32) % static_cast<uint64_t>(Senders)) + (HasOwnAlliance() ? 1 : 0);
		ReinforceList.push_back(R);
	}
	ReinforceAt = Time;
	bReinforceAsked = true;
	return Error::None;
}

WorldLand Kingdom::MakeWorldLand()
{
	const uint64_t H = Mix(Seed, WorldCounter++);
	WorldLand W;
	W.id = NextId++;
	// Land types by weight among those the Castle has opened (ruins and VP lands come later).
	const int Castle = CastleLevel();
	int64_t Total = 0;
	for (const LandTypeDef& T : C.landTypes)
	{
		Total += T.castle <= Castle ? T.weight : 0;
	}
	W.type = 0;
	int64_t Pick = Total > 0 ? static_cast<int64_t>(H % static_cast<uint64_t>(Total)) : 0;
	for (size_t I = 0; I < C.landTypes.size() && Total > 0; ++I)
	{
		const LandTypeDef& T = C.landTypes[I];
		if (T.castle > Castle || T.weight <= 0)
		{
			continue;
		}
		if (Pick < T.weight)
		{
			W.type = static_cast<int>(I);
			break;
		}
		Pick -= T.weight;
	}
	// Land levels follow the Castle: a band of three levels that rises with it.
	const int Lo = LandBandLow(Castle);
	const int Hi = LandBandTop(Castle);
	W.level = Lo + static_cast<int>((H >> 16) % static_cast<uint64_t>(Hi - Lo + 1));
	return W;
}

void Kingdom::FillWorld()
{
	while (static_cast<int>(WorldList.size()) < C.worldSize)
	{
		WorldList.push_back(MakeWorldLand());
	}
}

Error Kingdom::StartAttack(int32_t WorldId, bool Capture)
{
	return StartAttack(WorldId, Capture, std::vector<int>(), 0);
}

Error Kingdom::StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId)
{
	return StartAttack(WorldId, Capture, Squads, TitanId, std::vector<int>());
}

Error Kingdom::StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells)
{
	return StartAttack(WorldId, Capture, Squads, TitanId, Spells, false);
}

Error Kingdom::StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce)
{
	const WorldLand* W = nullptr;
	for (const WorldLand& It : WorldList)
	{
		if (It.id == WorldId)
		{
			W = &It;
		}
	}
	if (!W)
	{
		return Error::BadId;
	}
	if (Capture && static_cast<int>(LandList.size()) >= LandCapacity())
	{
		return Error::NoLandSlot;
	}
	return BeginBattle(WorldId, Capture, Capture ? C.captureStones : C.raidStones, Squads, TitanId, Spells, Reinforce);
}

Error Kingdom::BeginBattle(int32_t Target, bool Capture, int64_t Cost, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells,
	bool Reinforce)
{
	if (static_cast<int>(Squads.size()) > BattleSlots())
	{
		return Error::TooManySquads;
	}
	std::vector<int64_t> Need(C.troops.size(), 0);
	for (const int T : Squads)
	{
		if (T < 0 || T >= static_cast<int>(C.troops.size()))
		{
			return Error::BadId;
		}
		if (++Need[static_cast<size_t>(T)] > Army[static_cast<size_t>(T)])
		{
			return Error::NoTroops;
		}
	}
	if (TitanId != 0)
	{
		const TitanState* T = FindTitan(TitanId);
		if (!T)
		{
			return Error::BadId;
		}
		if (T->recoverUntil > Time)
		{
			return Error::TitanRecovering;
		}
	}
	if (static_cast<int>(Spells.size()) > C.battleSpellKinds)
	{
		return Error::TooManySquads;
	}
	for (size_t I = 0; I < Spells.size(); ++I)
	{
		const int Sp = Spells[I];
		if (Sp < 0 || Sp >= static_cast<int>(C.spells.size()) || std::count(Spells.begin(), Spells.end(), Sp) > 1)
		{
			return Error::BadId;
		}
		if (SpellStockList[static_cast<size_t>(Sp)] <= 0)
		{
			return Error::NoTroops;
		}
	}
	int64_t& Stones = Res_[static_cast<size_t>(ResIndex(Res::Stones))];
	if (Stones < Cost)
	{
		return Error::NeedStones;
	}
	const bool bWasFull = Stones >= Capacity(Res::Stones);
	Stones -= Cost;
	if (bWasFull)
	{
		StonesAt = Time;
	}
	Pending = Target;
	PendingIsCapture = Capture;
	PendingArmy = Squads;
	PendingTitanId = TitanId;
	PendingSpellList = Spells;
	// Allies' squads that have arrived go along; the ones still on their way stay in the camp's queue.
	PendingReinforce.clear();
	if (Reinforce)
	{
		for (size_t I = 0; I < ReinforceList.size();)
		{
			if (ReinforceList[I].at <= Time)
			{
				PendingReinforce.push_back(ReinforceList[I]);
				ReinforceList.erase(ReinforceList.begin() + static_cast<std::ptrdiff_t>(I));
			}
			else
			{
				++I;
			}
		}
	}
	NoteQuest(QuestEvent::Stones, Cost);
	NoteQuest(Target > 0 ? QuestEvent::Raid : Target <= -MissionBase ? QuestEvent::Mission : QuestEvent::Site, 1);
	return Error::None;
}

Error Kingdom::FinishAttack(int32_t WorldId, bool Capture, int Stars, Reward* Out)
{
	// Without survivor data every squad that went counts as intact.
	return FinishAttack(WorldId, Capture, Stars, std::vector<int>(PendingArmy.size(), 100), Out);
}

Error Kingdom::FinishAttack(int32_t WorldId, bool Capture, int Stars, const std::vector<int>& SurvivorPct, Reward* Out)
{
	return FinishAttack(WorldId, Capture, Stars, SurvivorPct, std::vector<int>(), Out);
}

void Kingdom::EndBattle(int Level, int Stars, int64_t XpPct, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward& R)
{
	if (Stars > 0)
	{
		NoteQuest(QuestEvent::Win, 1);
		NoteQuest(QuestEvent::ThreeStars, Stars >= 3 ? 1 : 0);
	}
	// Casualties: a squad comes back only if enough of it survived (Medical School threshold).
	const int Survival = SurvivalPct();
	for (size_t I = 0; I < PendingArmy.size(); ++I)
	{
		const int Pct = I < SurvivorPct.size() ? SurvivorPct[I] : 0;
		int64_t& Count = Army[static_cast<size_t>(PendingArmy[I])];
		if (Pct > 0 && Pct >= Survival)
		{
			++R.squadsBack;
		}
		else
		{
			Count = std::max<int64_t>(0, Count - 1);
			++R.squadsLost;
		}
	}
	// Spells cast come out of the stock.
	for (size_t I = 0; I < PendingSpellList.size(); ++I)
	{
		int64_t& Stock = SpellStockList[static_cast<size_t>(PendingSpellList[I])];
		Stock = std::max<int64_t>(0, Stock - std::max(0, I < SpellsUsed.size() ? SpellsUsed[I] : 0));
	}
	PendingSpellList.clear();
	UnblockCraft(Time);
	if (PendingTitanId != 0)
	{
		SettleLands(); // the titan may level up: its lands produce at the old rate until now
		for (TitanState& T : TitanList)
		{
			if (T.id != PendingTitanId)
			{
				continue;
			}
			// XP relics count for the battle they were worn in.
			const int64_t XpBonus = TitanRelicBonus(T.id, RelicStat::Xp);
			// Every working relic on the titan wears by one battle.
			for (const int32_t RelicId : T.relics)
			{
				for (RelicItem& Rl : RelicList)
				{
					if (Rl.id == RelicId && Rl.durability > 0 && --Rl.durability == 0)
					{
						++R.relicsBroken;
					}
				}
			}
			const int64_t Base = C.titanBattleXp[static_cast<size_t>(std::max(1, std::min<int>(Level, static_cast<int>(C.titanBattleXp.size()))) - 1)];
			R.titanId = T.id;
			R.titanXp = Base * XpPct / 100 * C.titanStarsPct[static_cast<size_t>(std::max(0, std::min(3, Stars)))] / 100 * (1000 + XpBonus) / 1000;
			AddTitanXp(T, R.titanXp, &R.titanLevels);
			if (T.level > C.titanRecoveryFromLevel)
			{
				// A relic on the Hall of Titans shortens the rest.
				T.recoverUntil = Time + Shorten((T.level - C.titanRecoveryFromLevel) * C.titanRecoverySecPerLevel, DefRelicBonus(C.hallOfTitans, RelicStat::Rest));
			}
		}
	}
	PendingArmy.clear();
	PendingTitanId = 0;
	UnblockQueue(Time);
	// Reinforcements help in one battle only (the original).
	R.reinforcements = static_cast<int>(PendingReinforce.size());
	PendingReinforce.clear();
}

Error Kingdom::FinishAttack(int32_t WorldId, bool Capture, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out)
{
	if (Pending <= 0 || Pending != WorldId || PendingIsCapture != Capture)
	{
		return Error::NoAttack;
	}
	Pending = 0;
	size_t Index = WorldList.size();
	for (size_t I = 0; I < WorldList.size(); ++I)
	{
		if (WorldList[I].id == WorldId)
		{
			Index = I;
		}
	}
	Reward R;
	EndBattle(Index < WorldList.size() ? WorldList[Index].level : 1, Stars, 100, SurvivorPct, SpellsUsed, R);
	if (Index >= WorldList.size())
	{
		if (Out)
		{
			*Out = R;
		}
		return Error::BadId;
	}
	if (Stars > 0)
	{
		const WorldLand W = WorldList[Index];
		const int64_t Pct = Stars >= 3 ? 100 : (Stars == 2 ? 75 : 50);
		const auto Scaled = [Pct](int64_t Base) { return Base > 0 ? std::max<int64_t>(1, Base * Pct / 100) : 0; };
		if (Capture)
		{
			Land L;
			L.id = NextId++;
			L.type = W.type;
			L.level = W.level;
			L.lastAt = Time;
			LandList.push_back(L);
			R.landId = L.id;
		}
		else
		{
			// A land rewards raiders with what it produces (the original since version 1.8).
			const Res Kind = C.landTypes[static_cast<size_t>(W.type)].resource;
			const int64_t Before = Amount(Kind);
			AddResource(Kind, Scaled(RaidLoot(W)));
			R.res[static_cast<size_t>(ResIndex(Kind))] += Amount(Kind) - Before;
		}
		// Every victory brings victory points as well (the original's rule; the amounts are ours).
		const int64_t Vp = Scaled(VictoryVp(W.level));
		Res_[static_cast<size_t>(ResIndex(Res::Vp))] += Vp;
		R.res[static_cast<size_t>(ResIndex(Res::Vp))] += Vp;
		// And relics: the original (1.8) drops one to three per won battle, better ones as the player goes on.
		// Here one per star; the rarity odds by land level are ours.
		if (!C.relicDropRarity.empty())
		{
			const std::array<int64_t, 5>& Odds = C.relicDropRarity[static_cast<size_t>(std::max(1, std::min<int>(W.level, static_cast<int>(C.relicDropRarity.size()))) - 1)];
			for (int K = 0; K < std::min(std::min(Stars, 3), C.relicDropMax); ++K)
			{
				const uint64_t H = Mix(Seed ^ 0x64726f70ULL, RelicCounter++);
				const int32_t Id = AddRelic(RollRelicKind(H >> 32), RollRarity(Odds, H));
				if (Id)
				{
					R.relics.push_back(Id);
				}
				else
				{
					++R.relicsLost;
				}
			}
		}
		// A beaten land leaves the map; a new one appears.
		WorldList.erase(WorldList.begin() + static_cast<std::ptrdiff_t>(Index));
		FillWorld();
	}
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

// --- Titan Monuments and XP Arenas -------------------------------------------------------------------

int64_t Kingdom::SiteReadyAt(int Site) const
{
	return Site >= 0 && Site < static_cast<int>(SiteReady.size()) ? SiteReady[static_cast<size_t>(Site)] : 0;
}

Check Kingdom::CheckSite(int Site) const
{
	Check Out;
	if (Site < 0 || Site >= SiteCount())
	{
		Out.error = Error::BadId;
		return Out;
	}
	const SiteDef& D = C.sites.list[static_cast<size_t>(Site)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Stones))] = C.sites.stones;
	if (CastleLevel() < D.castle)
	{
		Out.error = Error::Locked;
		Out.castle = D.castle;
	}
	else if (SiteReadyAt(Site) > Time)
	{
		Out.error = Error::NotReady;
		Out.timeSec = SiteReadyAt(Site) - Time;
	}
	else if (Amount(Res::Stones) < C.sites.stones)
	{
		Out.error = Error::NeedStones;
	}
	return Out;
}

int64_t Kingdom::SiteResetGems(int Site) const
{
	return SiteReadyAt(Site) > Time ? C.sites.list[static_cast<size_t>(Site)].resetGems : 0;
}

Error Kingdom::ResetSite(int Site)
{
	if (Site < 0 || Site >= SiteCount())
	{
		return Error::BadId;
	}
	if (SiteReadyAt(Site) <= Time)
	{
		return Error::Nothing;
	}
	const int64_t Gems = SiteResetGems(Site);
	if (Amount(Res::Gems) < Gems)
	{
		return Error::NotEnough;
	}
	Res_[static_cast<size_t>(ResIndex(Res::Gems))] -= Gems;
	SiteReady[static_cast<size_t>(Site)] = 0;
	return Error::None;
}

Error Kingdom::StartSiteAttack(int Site, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce)
{
	const Check Ck = CheckSite(Site);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	const Error E = BeginBattle(-(Site + 1), false, C.sites.stones, Squads, TitanId, Spells, Reinforce);
	if (E == Error::None)
	{
		// The original: once battled in, a monument or arena is closed for a day, whatever the result.
		SiteReady.resize(C.sites.list.size(), 0);
		SiteReady[static_cast<size_t>(Site)] = Time + C.sites.cooldownSec;
	}
	return E;
}

Error Kingdom::FinishSiteAttack(int Site, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out)
{
	if (Site < 0 || Site >= SiteCount() || Pending != -(Site + 1))
	{
		return Error::NoAttack;
	}
	Pending = 0;
	const SiteDef& D = C.sites.list[static_cast<size_t>(Site)];
	Reward R;
	EndBattle(D.level, Stars, D.arena ? D.xpPct : 100, SurvivorPct, SpellsUsed, R);
	if (Stars > 0 && !D.arena)
	{
		// A monument: victory points like a land of its level, and a chance of shards (the original 1.8: "a % drop
		// of a random Shard"; the chances and amounts are ours).
		const int64_t Pct = Stars >= 3 ? 100 : (Stars == 2 ? 75 : 50);
		const int64_t Vp = VictoryVp(D.level) > 0 ? std::max<int64_t>(1, VictoryVp(D.level) * Pct / 100) : 0;
		Res_[static_cast<size_t>(ResIndex(Res::Vp))] += Vp;
		R.res[static_cast<size_t>(ResIndex(Res::Vp))] += Vp;
		const uint64_t H = Mix(Seed ^ 0x6d6f6e75ULL, SiteCounter++);
		if (static_cast<int64_t>(H % 100) < C.sites.shardPct[static_cast<size_t>(std::min(3, Stars) - 1)])
		{
			const int64_t Before = Amount(Res::Shards);
			AddResource(Res::Shards, D.shards);
			R.res[static_cast<size_t>(ResIndex(Res::Shards))] += Amount(Res::Shards) - Before;
		}
	}
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

// --- Campaigns of the Adventurers' Guild -----------------------------------------------------------

int Kingdom::MissionCount(int Act) const
{
	return Act >= 0 && Act < ActCount() ? static_cast<int>(C.campaigns.acts[static_cast<size_t>(Act)].missions.size()) : 0;
}

int Kingdom::MissionStars(int Act, int Mission) const
{
	if (Act < 0 || Act >= static_cast<int>(MissionStarList.size()))
	{
		return 0;
	}
	const std::vector<uint8_t>& Stars = MissionStarList[static_cast<size_t>(Act)];
	return Mission >= 0 && Mission < static_cast<int>(Stars.size()) ? Stars[static_cast<size_t>(Mission)] : 0;
}

int Kingdom::ActStars(int Act) const
{
	int Total = 0;
	for (int M = 0; M < MissionCount(Act); ++M)
	{
		Total += MissionStars(Act, M);
	}
	return Total;
}

bool Kingdom::ChestClaimed(int Act, int Chest) const
{
	return Act >= 0 && Act < static_cast<int>(ChestMask.size()) && Chest >= 0 && Chest < 32 && ((ChestMask[static_cast<size_t>(Act)] >> Chest) & 1u) != 0;
}

Check Kingdom::CheckMission(int Act, int Mission) const
{
	Check Out;
	if (Mission < 0 || Mission >= MissionCount(Act))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const ActDef& A = C.campaigns.acts[static_cast<size_t>(Act)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Stones))] = C.campaigns.stones;
	// Missions go in order, and an act opens when the one before it is won to its last mission.
	const bool bOpen = Mission > 0 ? MissionStars(Act, Mission - 1) > 0 : (Act == 0 || MissionStars(Act - 1, MissionCount(Act - 1) - 1) > 0);
	if (C.campaigns.guild >= 0 && !BuildingLevel(C.campaigns.guild))
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.campaigns.guild, 1, 1};
	}
	else if (CastleLevel() < A.castle)
	{
		Out.error = Error::Locked;
		Out.castle = A.castle;
	}
	else if (!bOpen)
	{
		Out.error = Error::Locked;
	}
	else if (Amount(Res::Stones) < C.campaigns.stones)
	{
		Out.error = Error::NeedStones;
	}
	return Out;
}

Error Kingdom::StartMission(int Act, int Mission, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce)
{
	const Check Ck = CheckMission(Act, Mission);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	return BeginBattle(-(MissionBase + Act * 100 + Mission), false, C.campaigns.stones, Squads, TitanId, Spells, Reinforce);
}

Error Kingdom::FinishMission(int Act, int Mission, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out)
{
	if (Mission < 0 || Mission >= MissionCount(Act) || Pending != -(MissionBase + Act * 100 + Mission))
	{
		return Error::NoAttack;
	}
	Pending = 0;
	const MissionDef& D = C.campaigns.acts[static_cast<size_t>(Act)].missions[static_cast<size_t>(Mission)];
	Reward R;
	EndBattle(D.level, Stars, 100, SurvivorPct, SpellsUsed, R);
	MissionStarList.resize(C.campaigns.acts.size());
	std::vector<uint8_t>& Best = MissionStarList[static_cast<size_t>(Act)];
	Best.resize(static_cast<size_t>(MissionCount(Act)), 0);
	uint8_t& Mine = Best[static_cast<size_t>(Mission)];
	const uint8_t Won = static_cast<uint8_t>(std::max(0, std::min(3, Stars)));
	if (Won > 0 && Mine == 0)
	{
		// The first win pays the mission's reward (repeats give the titan XP only).
		R.firstClear = true;
		for (int I = 0; I < kResCount; ++I)
		{
			if (D.first[static_cast<size_t>(I)] > 0)
			{
				const int64_t Before = Amount(static_cast<Res>(I));
				AddResource(static_cast<Res>(I), D.first[static_cast<size_t>(I)]);
				R.res[static_cast<size_t>(I)] += Amount(static_cast<Res>(I)) - Before;
			}
		}
	}
	Mine = std::max(Mine, Won);
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

Error Kingdom::ClaimChest(int Act, int Chest, Reward* Out)
{
	if (Act < 0 || Act >= ActCount() || Chest < 0 || Chest >= static_cast<int>(C.campaigns.acts[static_cast<size_t>(Act)].chests.size()))
	{
		return Error::BadId;
	}
	if (ChestClaimed(Act, Chest))
	{
		return Error::Nothing;
	}
	const ChestDef& D = C.campaigns.acts[static_cast<size_t>(Act)].chests[static_cast<size_t>(Chest)];
	if (ActStars(Act) < D.stars)
	{
		return Error::NotEnough;
	}
	// A titan while the Hall of Titans has room, else a relic of its rarity; a relic needs room in the inventory.
	const uint64_t H = Mix(Seed ^ 0x6368657374ULL, static_cast<uint64_t>(Act * 32 + Chest));
	const bool bTitan = D.titan > 0 && static_cast<int>(TitanList.size()) < TitanCapacity();
	const int RelicRarity = bTitan ? D.relic : std::max(D.relic, D.titan);
	if (RelicRarity > 0 && static_cast<int>(RelicList.size()) >= C.maxRelics)
	{
		return Error::InventoryFull;
	}
	Reward R;
	for (int I = 0; I < kResCount; ++I)
	{
		if (D.res[static_cast<size_t>(I)] > 0)
		{
			const int64_t Before = Amount(static_cast<Res>(I));
			AddResource(static_cast<Res>(I), D.res[static_cast<size_t>(I)]);
			R.res[static_cast<size_t>(I)] += Amount(static_cast<Res>(I)) - Before;
		}
	}
	if (bTitan)
	{
		R.newTitan = SummonTitan(D.titan, H >> 24);
	}
	if (RelicRarity > 0)
	{
		const int32_t Id = AddRelic(RollRelicKind(H >> 32), RelicRarity);
		if (Id != 0)
		{
			R.relics.push_back(Id);
		}
	}
	ChestMask.resize(C.campaigns.acts.size(), 0);
	ChestMask[static_cast<size_t>(Act)] |= 1u << Chest;
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

// --- Daily quests ----------------------------------------------------------------------------------

bool Kingdom::QuestOpen(int Def) const
{
	const QuestDef& D = C.quests.pool[static_cast<size_t>(Def)];
	return CastleLevel() >= D.castle && (D.building < 0 || BuildingLevel(D.building));
}

int64_t Kingdom::QuestTarget(int Def) const
{
	if (Def < 0 || Def >= static_cast<int>(C.quests.pool.size()))
	{
		return 0;
	}
	const std::vector<std::pair<int, int64_t>>& Steps = C.quests.pool[static_cast<size_t>(Def)].target;
	int64_t Target = Steps.empty() ? 1 : Steps.front().second;
	for (const std::pair<int, int64_t>& Step : Steps)
	{
		if (CastleLevel() >= Step.first)
		{
			Target = Step.second;
		}
	}
	return std::max<int64_t>(1, Target);
}

void Kingdom::RollQuests()
{
	const int64_t Day = std::max<int64_t>(0, Time) / 86400;
	if (Day == QuestDay)
	{
		return;
	}
	QuestDay = Day;
	QuestList.clear();
	QuestRerolls = 0;
	bQuestChest = false;
	std::vector<int> Open;
	for (int I = 0; I < static_cast<int>(C.quests.pool.size()); ++I)
	{
		if (QuestOpen(I))
		{
			Open.push_back(I);
		}
	}
	for (int N = 0; N < C.quests.count && !Open.empty(); ++N)
	{
		const uint64_t H = Mix(Seed ^ 0x7175657374ULL, static_cast<uint64_t>(Day) * 16 + static_cast<uint64_t>(N));
		const size_t Pick = static_cast<size_t>(H % Open.size());
		DailyQuest Q;
		Q.def = Open[Pick];
		Q.target = QuestTarget(Q.def);
		QuestList.push_back(Q);
		Open.erase(Open.begin() + static_cast<std::ptrdiff_t>(Pick));
	}
}

void Kingdom::NoteQuest(QuestEvent Event, int64_t Amount)
{
	if (Amount == 0 || Event == QuestEvent::Count)
	{
		return;
	}
	for (DailyQuest& Q : QuestList)
	{
		if (!Q.claimed && C.quests.pool[static_cast<size_t>(Q.def)].event == Event)
		{
			Q.progress = std::max<int64_t>(0, Q.progress + Amount);
		}
	}
}

Error Kingdom::ClaimQuest(int Slot, Reward* Out)
{
	if (Slot < 0 || Slot >= static_cast<int>(QuestList.size()))
	{
		return Error::BadId;
	}
	DailyQuest& Q = QuestList[static_cast<size_t>(Slot)];
	if (Q.claimed)
	{
		return Error::Nothing;
	}
	if (Q.progress < Q.target)
	{
		return Error::NotReady;
	}
	Reward R;
	const ResArray& Pay = C.quests.pool[static_cast<size_t>(Q.def)].reward;
	for (int I = 0; I < kResCount; ++I)
	{
		if (Pay[static_cast<size_t>(I)] > 0)
		{
			const int64_t Before = Amount(static_cast<Res>(I));
			AddResource(static_cast<Res>(I), Pay[static_cast<size_t>(I)]);
			R.res[static_cast<size_t>(I)] += Amount(static_cast<Res>(I)) - Before;
		}
	}
	Q.claimed = true;
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

Error Kingdom::RerollQuest(int Slot)
{
	if (Slot < 0 || Slot >= static_cast<int>(QuestList.size()))
	{
		return Error::BadId;
	}
	DailyQuest& Q = QuestList[static_cast<size_t>(Slot)];
	if (Q.claimed || Q.progress >= Q.target)
	{
		return Error::Nothing;
	}
	if (QuestRerolls >= C.quests.rerolls)
	{
		return Error::LimitReached;
	}
	std::vector<int> Open;
	for (int I = 0; I < static_cast<int>(C.quests.pool.size()); ++I)
	{
		const bool bTaken = std::any_of(QuestList.begin(), QuestList.end(), [I](const DailyQuest& X) { return X.def == I; });
		if (!bTaken && QuestOpen(I))
		{
			Open.push_back(I);
		}
	}
	if (Open.empty())
	{
		return Error::NotEnough;
	}
	const uint64_t H = Mix(Seed ^ 0x72656c6c6fULL, QuestCounter++);
	Q = DailyQuest();
	Q.def = Open[static_cast<size_t>(H % Open.size())];
	Q.target = QuestTarget(Q.def);
	++QuestRerolls;
	return Error::None;
}

Error Kingdom::ClaimQuestChest(Reward* Out)
{
	if (bQuestChest)
	{
		return Error::Nothing;
	}
	if (QuestList.empty() || std::any_of(QuestList.begin(), QuestList.end(), [](const DailyQuest& Q) { return !Q.claimed; }))
	{
		return Error::NotReady;
	}
	int Rarity = 0;
	for (const std::pair<int, int>& Step : C.quests.chestRelic)
	{
		if (CastleLevel() >= Step.first)
		{
			Rarity = Step.second;
		}
	}
	if (Rarity > 0 && static_cast<int>(RelicList.size()) >= C.maxRelics)
	{
		return Error::InventoryFull;
	}
	Reward R;
	for (int I = 0; I < kResCount; ++I)
	{
		if (C.quests.chest[static_cast<size_t>(I)] > 0)
		{
			const int64_t Before = Amount(static_cast<Res>(I));
			AddResource(static_cast<Res>(I), C.quests.chest[static_cast<size_t>(I)]);
			R.res[static_cast<size_t>(I)] += Amount(static_cast<Res>(I)) - Before;
		}
	}
	if (Rarity > 0)
	{
		const uint64_t H = Mix(Seed ^ 0x7163686573ULL, static_cast<uint64_t>(QuestDay));
		const int32_t Id = AddRelic(RollRelicKind(H >> 32), Rarity);
		if (Id != 0)
		{
			R.relics.push_back(Id);
		}
	}
	bQuestChest = true;
	if (Out)
	{
		*Out = R;
	}
	return Error::None;
}

// --- Army -----------------------------------------------------------------------------------------

const LevelDef* Kingdom::BuildingLevel(int Def) const
{
	const LevelDef* Best = nullptr;
	for (const Building& B : BuildingList)
	{
		if (B.def == Def)
		{
			const LevelDef* L = CurrentLevel(B);
			if (L && (!Best || L->level > Best->level))
			{
				Best = L;
			}
		}
	}
	return Best;
}

int Kingdom::TroopLevel(int Troop) const
{
	return Troop >= 0 && Troop < static_cast<int>(TroopLevels.size()) ? TroopLevels[static_cast<size_t>(Troop)] : 0;
}

int64_t Kingdom::ArmyCount(int Troop) const
{
	return Troop >= 0 && Troop < static_cast<int>(Army.size()) ? Army[static_cast<size_t>(Troop)] : 0;
}

int64_t Kingdom::ArmyTotal() const
{
	int64_t Total = 0;
	for (const int64_t N : Army)
	{
		Total += N;
	}
	return Total;
}

int64_t Kingdom::ArmyCapacity() const
{
	int64_t Total = 0;
	for (const Building& B : BuildingList)
	{
		if (B.def == C.armyCamp)
		{
			if (const LevelDef* L = CurrentLevel(B))
			{
				Total += L->troops;
			}
		}
	}
	return Total;
}

int Kingdom::BattleSlots() const
{
	const LevelDef* L = BuildingLevel(C.armyCamp);
	return L ? static_cast<int>(L->slots) : 0;
}

int Kingdom::QueueCapacity() const
{
	const LevelDef* L = BuildingLevel(C.barracks);
	return L ? static_cast<int>(L->queue) : 0;
}

int Kingdom::MaxTroopLevel() const
{
	const LevelDef* L = BuildingLevel(C.armoury);
	return L ? static_cast<int>(std::max<int64_t>(1, L->maxTroopLevel)) : 1;
}

int Kingdom::CouncilLevel(int Troop) const
{
	if (Troop < 0 || Troop >= static_cast<int>(C.troops.size()) || C.troops[static_cast<size_t>(Troop)].council < 0)
	{
		return -1;
	}
	const LevelDef* L = BuildingLevel(C.troops[static_cast<size_t>(Troop)].council);
	return L ? L->level : 0;
}

int Kingdom::TroopLevelCap(int Troop) const
{
	int Cap = MaxTroopLevel();
	if (CouncilLevel(Troop) >= 0)
	{
		const LevelDef* L = BuildingLevel(C.troops[static_cast<size_t>(Troop)].council);
		Cap = L && L->maxTroopLevel > 0 ? std::min<int>(Cap, static_cast<int>(L->maxTroopLevel)) : (L ? Cap : 1);
	}
	return Cap;
}

int Kingdom::SurvivalPct() const
{
	const LevelDef* L = BuildingLevel(C.medicalSchool);
	return L && L->survival > 0 ? static_cast<int>(L->survival) : 100;
}

int64_t Kingdom::TrainSec(int Troop) const
{
	const TroopType& T = C.troops[static_cast<size_t>(Troop)];
	const int64_t Sec = T.levels[static_cast<size_t>(std::max(1, std::min(TroopLevel(Troop), T.MaxLevel())) - 1)].trainSec;
	// A relic on the Barracks speeds up every troop; one on an envoy council, that council's troops.
	const int64_t Bonus = DefRelicBonus(C.barracks, RelicStat::Train) + (T.council >= 0 ? DefRelicBonus(T.council, RelicStat::Train) : 0);
	return Shorten(Sec, CapBonus(RelicStat::Train, Bonus));
}

int64_t Kingdom::QueueHeadFinish() const
{
	return Queue.empty() ? 0 : QueueStart + TrainSec(Queue.front());
}

int64_t Kingdom::GemsForSeconds(int64_t Seconds) const
{
	return Seconds <= 0 ? 0 : std::max<int64_t>(1, CeilDiv(Seconds, C.skipSecPerGem));
}

void Kingdom::ProcessQueue(int64_t Until)
{
	while (!Queue.empty() && !bQueueBlocked)
	{
		const int64_t Done = QueueStart + TrainSec(Queue.front());
		if (Done > Until)
		{
			break;
		}
		if (ArmyTotal() >= ArmyCapacity())
		{
			bQueueBlocked = true; // the unit waits for room in the Army Camp
			break;
		}
		Army[static_cast<size_t>(Queue.front())] += 1;
		Queue.erase(Queue.begin());
		QueueStart = Done;
	}
}

void Kingdom::UnblockQueue(int64_t At)
{
	if (bQueueBlocked && ArmyTotal() < ArmyCapacity())
	{
		bQueueBlocked = false;
		if (!Queue.empty())
		{
			QueueStart = At - TrainSec(Queue.front());
		}
		ProcessQueue(At);
	}
}

Check Kingdom::CheckTrain(int Troop) const
{
	Check Out;
	if (Troop < 0 || Troop >= static_cast<int>(C.troops.size()))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const TroopType& T = C.troops[static_cast<size_t>(Troop)];
	const TroopLevelDef& L = T.levels[static_cast<size_t>(std::max(1, TroopLevel(Troop)) - 1)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Food))] = L.cost;
	Out.timeSec = TrainSec(Troop);
	if (CastleLevel() < T.unlockCastle)
	{
		Out.error = Error::Locked;
		Out.castle = T.unlockCastle;
		return Out;
	}
	if (T.council >= 0 && CouncilLevel(Troop) < T.councilLevel)
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {T.council, T.councilLevel, 1};
		return Out;
	}
	if (QueueCapacity() <= 0)
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.barracks, 1, 1};
		return Out;
	}
	if (static_cast<int>(Queue.size()) >= QueueCapacity())
	{
		Out.error = Error::QueueFull;
		return Out;
	}
	if (Amount(Res::Food) < L.cost)
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Food))] = L.cost - Amount(Res::Food);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

Error Kingdom::Train(int Troop)
{
	const Check Ck = CheckTrain(Troop);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	Pay(Ck.cost, false);
	if (Queue.empty())
	{
		QueueStart = Time;
		bQueueBlocked = false;
	}
	Queue.push_back(Troop);
	ProcessQueue(Time);
	NoteQuest(QuestEvent::Train, 1);
	return Error::None;
}

Error Kingdom::CancelTrain(int Index)
{
	if (Index < 0 || Index >= static_cast<int>(Queue.size()))
	{
		return Error::BadId;
	}
	const int Troop = Queue[static_cast<size_t>(Index)];
	const TroopType& T = C.troops[static_cast<size_t>(Troop)];
	Res_[static_cast<size_t>(ResIndex(Res::Food))] += T.levels[static_cast<size_t>(std::max(1, TroopLevel(Troop)) - 1)].cost;
	Queue.erase(Queue.begin() + Index);
	NoteQuest(QuestEvent::Train, -1); // a cancelled squad does not count
	if (Index == 0)
	{
		QueueStart = Time;
		bQueueBlocked = false;
		ProcessQueue(Time);
	}
	return Error::None;
}

int64_t Kingdom::TrainSkipCost() const
{
	const int64_t Room = std::max<int64_t>(0, ArmyCapacity() - ArmyTotal());
	const size_t N = static_cast<size_t>(std::min<int64_t>(Room, static_cast<int64_t>(Queue.size())));
	int64_t Seconds = 0;
	for (size_t I = 0; I < N; ++I)
	{
		Seconds += I == 0 ? std::max<int64_t>(0, QueueHeadFinish() - Time) : TrainSec(Queue[I]);
	}
	return N == 0 ? 0 : std::max<int64_t>(1, GemsForSeconds(Seconds));
}

Error Kingdom::SkipTraining()
{
	if (Queue.empty())
	{
		return Error::Nothing;
	}
	const int64_t Room = std::max<int64_t>(0, ArmyCapacity() - ArmyTotal());
	if (Room <= 0)
	{
		return Error::LimitReached;
	}
	const int64_t Gems = TrainSkipCost();
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	if (Have < Gems)
	{
		return Error::NotEnough;
	}
	Have -= Gems;
	for (int64_t I = 0; I < Room && !Queue.empty(); ++I)
	{
		Army[static_cast<size_t>(Queue.front())] += 1;
		Queue.erase(Queue.begin());
	}
	QueueStart = Time;
	bQueueBlocked = false;
	ProcessQueue(Time);
	return Error::None;
}

Error Kingdom::DismissTroops(int Troop, int Count)
{
	if (Troop < 0 || Troop >= static_cast<int>(C.troops.size()) || Count <= 0)
	{
		return Error::BadId;
	}
	int64_t Away = 0;
	for (const int T : PendingArmy)
	{
		Away += T == Troop ? 1 : 0;
	}
	int64_t& N = Army[static_cast<size_t>(Troop)];
	const int64_t Take = std::min<int64_t>(Count, N - Away);
	if (Take <= 0)
	{
		return Error::NoTroops;
	}
	N -= Take;
	UnblockQueue(Time);
	return Error::None;
}

Check Kingdom::CheckTroopUpgrade(int Troop) const
{
	Check Out;
	if (Troop < 0 || Troop >= static_cast<int>(C.troops.size()))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const TroopType& T = C.troops[static_cast<size_t>(Troop)];
	const int Level = TroopLevel(Troop);
	if (Level >= T.MaxLevel())
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	const TroopLevelDef& L = T.levels[static_cast<size_t>(Level - 1)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Food))] = L.upgradeCost;
	Out.timeSec = Shorten(L.upgradeSec, DefRelicBonus(C.armoury, RelicStat::Research));
	if (CastleLevel() < T.unlockCastle)
	{
		Out.error = Error::Locked;
		Out.castle = T.unlockCastle;
		return Out;
	}
	if (T.council >= 0 && Level + 1 > TroopLevelCap(Troop) && Level + 1 <= MaxTroopLevel() && BuildingLevel(C.armoury))
	{
		// The council level that allows the next troop level.
		Out.error = Error::NeedRequirement;
		Out.missing = {T.council, std::max(T.councilLevel, CouncilLevel(Troop) + 1), 1};
		for (const LevelDef& A : C.buildings[static_cast<size_t>(T.council)].levels)
		{
			if (A.maxTroopLevel >= Level + 1)
			{
				Out.missing.level = std::max(T.councilLevel, A.level);
				break;
			}
		}
		return Out;
	}
	if (!BuildingLevel(C.armoury) || Level + 1 > MaxTroopLevel())
	{
		// The Armoury level that allows the next troop level.
		Out.error = Error::NeedRequirement;
		Out.missing = {C.armoury, 1, 1};
		if (C.armoury >= 0)
		{
			for (const LevelDef& A : C.buildings[static_cast<size_t>(C.armoury)].levels)
			{
				if (A.maxTroopLevel >= Level + 1)
				{
					Out.missing.level = A.level;
					break;
				}
			}
		}
		return Out;
	}
	if (Research >= 0)
	{
		Out.error = Error::Researching;
		return Out;
	}
	if (Amount(Res::Food) < L.upgradeCost)
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Food))] = L.upgradeCost - Amount(Res::Food);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

Error Kingdom::UpgradeTroop(int Troop, bool UseGems)
{
	const Check Ck = CheckTroopUpgrade(Troop);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!Pay(Ck.cost, UseGems))
	{
		return Error::NotEnough;
	}
	Research = Troop;
	ResearchFrom = Time;
	ResearchUntil = Time + Ck.timeSec;
	if (Ck.timeSec <= 0)
	{
		TroopLevels[static_cast<size_t>(Troop)] += 1;
		Research = -1;
	}
	NoteQuest(QuestEvent::Upgrade, 1);
	return Error::None;
}

int64_t Kingdom::ResearchSkipCost() const
{
	return Research < 0 ? 0 : GemsForSeconds(std::max<int64_t>(0, ResearchUntil - Time));
}

Error Kingdom::SkipResearch()
{
	if (Research < 0)
	{
		return Error::NotBusy;
	}
	const int64_t Gems = ResearchSkipCost();
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	if (Have < Gems)
	{
		return Error::NotEnough;
	}
	Have -= Gems;
	TroopLevels[static_cast<size_t>(Research)] = std::min(TroopLevels[static_cast<size_t>(Research)] + 1, C.troops[static_cast<size_t>(Research)].MaxLevel());
	Research = -1;
	return Error::None;
}

// --- Titans ---------------------------------------------------------------------------------------

const TitanState* Kingdom::FindTitan(int32_t Id) const
{
	for (const TitanState& T : TitanList)
	{
		if (T.id == Id)
		{
			return &T;
		}
	}
	return nullptr;
}

int Kingdom::TitanCapacity() const
{
	const LevelDef* L = BuildingLevel(C.hallOfTitans);
	return L ? static_cast<int>(L->titans) : 0;
}

int64_t Kingdom::XpToNext(int Level) const
{
	const size_t I = static_cast<size_t>(std::max(1, Level) - 1);
	return I < C.titanXpNext.size() ? C.titanXpNext[I] : C.titanXpNext.back();
}

int64_t Kingdom::TitanTotalXp(const TitanState& T) const
{
	int64_t Total = T.xp;
	for (int L = 1; L < T.level; ++L)
	{
		Total += XpToNext(L);
	}
	return Total;
}

int Kingdom::TitanMaxLevel(const TitanState& T) const
{
	return C.titans[static_cast<size_t>(T.def)].MaxLevel() + (T.ascended ? C.ascendLevels : 0);
}

void Kingdom::AddTitanXp(TitanState& T, int64_t Xp, int* OutLevels)
{
	const int Max = TitanMaxLevel(T);
	T.xp += std::max<int64_t>(0, Xp);
	while (T.level < Max && T.xp >= XpToNext(T.level))
	{
		T.xp -= XpToNext(T.level);
		++T.level;
		if (OutLevels)
		{
			++*OutLevels;
		}
	}
	if (T.level >= Max)
	{
		T.xp = 0;
	}
}

int64_t Kingdom::TitanSkipCost(int32_t Id) const
{
	const TitanState* T = FindTitan(Id);
	return T && T->recoverUntil > Time ? GemsForSeconds(T->recoverUntil - Time) : 0;
}

Error Kingdom::SkipRecovery(int32_t TitanId)
{
	for (TitanState& T : TitanList)
	{
		if (T.id != TitanId)
		{
			continue;
		}
		if (T.recoverUntil <= Time)
		{
			return Error::NotBusy;
		}
		const int64_t Gems = TitanSkipCost(TitanId);
		int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
		if (Have < Gems)
		{
			return Error::NotEnough;
		}
		Have -= Gems;
		T.recoverUntil = Time;
		return Error::None;
	}
	return Error::BadId;
}

Check Kingdom::CheckAscend(int32_t TitanId) const
{
	Check Out;
	const TitanState* T = FindTitan(TitanId);
	if (!T)
	{
		Out.error = Error::BadId;
		return Out;
	}
	const size_t Rarity = static_cast<size_t>(std::max(1, std::min(4, C.titans[static_cast<size_t>(T->def)].stars)) - 1);
	Out.cost[static_cast<size_t>(ResIndex(Res::Shards))] = C.ascendShards[Rarity];
	Out.cost[static_cast<size_t>(ResIndex(Res::Food))] = C.ascendFood[Rarity];
	if (T->ascended)
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	// Only a titan at its level cap can ascend (wiki).
	if (T->level < TitanMaxLevel(*T))
	{
		Out.error = Error::NotMaxLevel;
		return Out;
	}
	if (TitanId == PendingTitanId)
	{
		Out.error = Error::Busy;
		return Out;
	}
	ResArray Missing{};
	bool bMissing = false;
	for (int I = 0; I < kResCount; ++I)
	{
		Missing[static_cast<size_t>(I)] = std::max<int64_t>(0, Out.cost[static_cast<size_t>(I)] - Res_[static_cast<size_t>(I)]);
		bMissing = bMissing || Missing[static_cast<size_t>(I)] > 0;
	}
	if (bMissing)
	{
		Out.error = Error::NotEnough;
		Out.gemsForMissing = OnlyBuyableMissing(Missing) ? GemsForResources(Missing) : -1;
	}
	return Out;
}

Error Kingdom::Ascend(int32_t TitanId, bool UseGems)
{
	const Check Ck = CheckAscend(TitanId);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!Pay(Ck.cost, UseGems))
	{
		return Error::NotEnough;
	}
	for (TitanState& T : TitanList)
	{
		if (T.id == TitanId)
		{
			T.ascended = true;
			T.xp = 0;
			AddSkill(T, Mix(Seed ^ 0x736b696c6cULL, static_cast<uint64_t>(T.id) * 8 + T.skills.size()));
		}
	}
	return Error::None;
}

Check Kingdom::CheckAltar(int Altar) const
{
	Check Out;
	if (Altar < 0 || Altar >= static_cast<int>(C.altars.size()))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const AltarDef& A = C.altars[static_cast<size_t>(Altar)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Gems))] = A.gems;
	if (!BuildingLevel(C.relicTemple))
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.relicTemple, 1, 1};
		return Out;
	}
	if (A.relics && static_cast<int>(RelicList.size()) >= C.maxRelics)
	{
		Out.error = Error::InventoryFull;
		return Out;
	}
	if (!A.relics && static_cast<int>(TitanList.size()) >= TitanCapacity())
	{
		Out.error = Error::RosterFull;
		return Out;
	}
	if (A.cooldownSec > 0 && RelicAt > Time)
	{
		Out.error = Error::NotReady;
		Out.timeSec = RelicAt - Time;
		return Out;
	}
	if (Amount(Res::Gems) < A.gems)
	{
		Out.error = Error::NotEnough;
		Out.gemsForMissing = -1;
	}
	return Out;
}

Error Kingdom::OpenAltar(int Altar, int32_t* OutTitanId, int32_t* OutRelicId)
{
	const Check Ck = CheckAltar(Altar);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	const AltarDef& A = C.altars[static_cast<size_t>(Altar)];
	Res_[static_cast<size_t>(ResIndex(Res::Gems))] -= A.gems;
	const uint64_t H = Mix(Seed ^ 0x72656c6963ULL, RelicCounter++);
	if (OutTitanId)
	{
		*OutTitanId = 0;
	}
	if (OutRelicId)
	{
		*OutRelicId = 0;
	}
	// The free daily relic is either a relic item or a titan (the original's daily bonus);
	// the Altar of Power always gives a relic item (the original's 1.8).
	const bool bDailyRelic = A.cooldownSec > 0 && static_cast<int64_t>((H >> 40) % 100) < C.dailyRelicPct && static_cast<int>(RelicList.size()) < C.maxRelics;
	if (A.relics || bDailyRelic)
	{
		const int32_t Id = AddRelic(RollRelicKind(H >> 32), RollRarity(A.relics ? A.weights : C.dailyRelicRarity, H >> 8));
		if (A.cooldownSec > 0)
		{
			RelicAt = Time + A.cooldownSec;
		}
		if (OutRelicId)
		{
			*OutRelicId = Id;
		}
		return Error::None;
	}
	int64_t Total = 0;
	for (const int64_t W : A.weights)
	{
		Total += W;
	}
	int Stars = 1;
	if (Total > 0)
	{
		int64_t Roll = static_cast<int64_t>(H % static_cast<uint64_t>(Total));
		for (int K = 0; K < 4; ++K)
		{
			if (Roll < A.weights[static_cast<size_t>(K)])
			{
				Stars = K + 1;
				break;
			}
			Roll -= A.weights[static_cast<size_t>(K)];
		}
	}
	const int32_t Id = SummonTitan(Stars, H >> 24);
	if (A.cooldownSec > 0)
	{
		RelicAt = Time + A.cooldownSec;
	}
	if (OutTitanId)
	{
		*OutTitanId = Id;
	}
	return Error::None;
}

int32_t Kingdom::SummonTitan(int Stars, uint64_t Pick)
{
	std::vector<int> Pool;
	for (size_t I = 0; I < C.titans.size(); ++I)
	{
		if (C.titans[I].stars == Stars)
		{
			Pool.push_back(static_cast<int>(I));
		}
	}
	if (Pool.empty())
	{
		for (size_t I = 0; I < C.titans.size(); ++I)
		{
			Pool.push_back(static_cast<int>(I));
		}
	}
	TitanState T;
	T.id = NextId++;
	T.def = Pool[static_cast<size_t>(Pick % static_cast<uint64_t>(Pool.size()))];
	StartSkills(T);
	TitanList.push_back(T);
	return T.id;
}

Check Kingdom::CheckFusion(int32_t Target, const std::vector<int32_t>& Sacrifices) const
{
	Check Out;
	const TitanState* T = FindTitan(Target);
	if (!T || Sacrifices.empty())
	{
		Out.error = Error::BadId;
		return Out;
	}
	for (size_t I = 0; I < Sacrifices.size(); ++I)
	{
		const TitanState* S = FindTitan(Sacrifices[I]);
		if (!S || Sacrifices[I] == Target || Sacrifices[I] == PendingTitanId || std::count(Sacrifices.begin(), Sacrifices.end(), Sacrifices[I]) > 1)
		{
			Out.error = Error::BadId;
			return Out;
		}
		Out.cost[static_cast<size_t>(ResIndex(Res::Food))] += C.fusionFoodPerStar * C.titans[static_cast<size_t>(S->def)].stars;
	}
	if (CastleLevel() < C.fusionCastle)
	{
		Out.error = Error::CastleTooLow;
		Out.castle = C.fusionCastle;
		return Out;
	}
	if (T->level >= TitanMaxLevel(*T))
	{
		Out.error = Error::MaxLevel;
		return Out;
	}
	const int64_t Food = Out.cost[static_cast<size_t>(ResIndex(Res::Food))];
	if (Amount(Res::Food) < Food)
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Food))] = Food - Amount(Res::Food);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

Error Kingdom::FuseTitans(int32_t Target, const std::vector<int32_t>& Sacrifices)
{
	const Check Ck = CheckFusion(Target, Sacrifices);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	Pay(Ck.cost, false);
	SettleLands();
	for (Land& L : LandList)
	{
		if (std::find(Sacrifices.begin(), Sacrifices.end(), L.titanId) != Sacrifices.end())
		{
			L.titanId = 0;
		}
	}
	for (const int32_t Id : Sacrifices)
	{
		ClearGuard(Id);
	}
	int64_t Xp = 0;
	std::vector<int> FodderStars;
	for (const int32_t Id : Sacrifices)
	{
		const TitanState* S = FindTitan(Id);
		const int64_t Stars = C.titans[static_cast<size_t>(S->def)].stars;
		Xp += C.fusionXpPerStarSq * Stars * Stars + TitanTotalXp(*S) * C.fusionKeepXpPct / 100;
		FodderStars.push_back(static_cast<int>(Stars));
	}
	TitanList.erase(std::remove_if(TitanList.begin(), TitanList.end(),
		[&Sacrifices](const TitanState& T) { return std::find(Sacrifices.begin(), Sacrifices.end(), T.id) != Sacrifices.end(); }), TitanList.end());
	for (TitanState& T : TitanList)
	{
		if (T.id == Target)
		{
			AddTitanXp(T, Xp, nullptr);
			// Skills: each fused titan may open the next slot (1 star for the 2nd, 2 for the 3rd, 3 for the 4th) or else
			// raise a skill up to its own stars (the original's guides; the chances are ours).
			for (const int Stars : FodderStars)
			{
				const uint64_t H = Mix(Seed ^ 0x6675736500ULL, RelicCounter++);
				const int BaseOpen = static_cast<int>(T.skills.size()) - (T.ascended ? 1 : 0);
				if (BaseOpen < C.skills.slots && Stars >= BaseOpen && static_cast<int>(H % 100) < C.skills.openPct)
				{
					AddSkill(T, H >> 16);
					if (T.ascended && T.skills.size() >= 2)
					{
						std::swap(T.skills[T.skills.size() - 1], T.skills[T.skills.size() - 2]); // the ascension skill stays last
					}
					continue;
				}
				std::vector<size_t> Up;
				for (size_t I = 0; I < T.skills.size(); ++I)
				{
					if (T.skills[I].tier < std::min(3, Stars))
					{
						Up.push_back(I);
					}
				}
				if (!Up.empty() && static_cast<int>((H >> 8) % 100) < C.skills.upgradePct)
				{
					T.skills[Up[static_cast<size_t>((H >> 32) % Up.size())]].tier += 1;
				}
			}
		}
	}
	return Error::None;
}

// --- Titan skills ---------------------------------------------------------------------------------

int Kingdom::SkillSlots(const TitanState& T) const
{
	return C.skills.kinds.empty() ? 0 : C.skills.slots + (T.ascended ? 1 : 0);
}

int64_t Kingdom::SkillValue(const TitanSkill& S) const
{
	if (S.kind < 0 || S.kind >= static_cast<int>(C.skills.kinds.size()))
	{
		return 0;
	}
	return C.skills.kinds[static_cast<size_t>(S.kind)].tiers[static_cast<size_t>(std::max(1, std::min(3, S.tier)) - 1)];
}

void Kingdom::AddSkill(TitanState& T, uint64_t H) const
{
	if (static_cast<int>(T.skills.size()) >= SkillSlots(T))
	{
		return;
	}
	const std::string& Cls = C.titans[static_cast<size_t>(T.def)].cls;
	const std::vector<int64_t>* Weights = nullptr;
	for (const std::pair<std::string, std::vector<int64_t>>& W : C.skills.classWeights)
	{
		Weights = W.first == Cls ? &W.second : Weights;
	}
	std::vector<int64_t> W(C.skills.kinds.size(), 0);
	int64_t Total = 0;
	for (size_t K = 0; K < W.size(); ++K)
	{
		const bool bTaken = std::any_of(T.skills.begin(), T.skills.end(), [K](const TitanSkill& S) { return S.kind == static_cast<int>(K); });
		W[K] = bTaken ? 0 : Weights ? (*Weights)[K] : 1;
		Total += W[K];
	}
	if (Total <= 0)
	{
		return;
	}
	int64_t Roll = static_cast<int64_t>(H % static_cast<uint64_t>(Total));
	for (size_t K = 0; K < W.size(); ++K)
	{
		if (Roll < W[K])
		{
			TitanSkill S;
			S.kind = static_cast<int>(K);
			T.skills.push_back(S);
			return;
		}
		Roll -= W[K];
	}
}

void Kingdom::StartSkills(TitanState& T) const
{
	AddSkill(T, Mix(Seed ^ 0x736b696c6cULL, static_cast<uint64_t>(T.id) * 8));
	if (T.ascended)
	{
		AddSkill(T, Mix(Seed ^ 0x736b696c6cULL, static_cast<uint64_t>(T.id) * 8 + T.skills.size()));
	}
}

int64_t Kingdom::TitanSkillBonus(int32_t TitanId, RelicStat Stat) const
{
	const TitanState* T = FindTitan(TitanId);
	int64_t Total = 0;
	for (size_t I = 0; T && I < T->skills.size(); ++I)
	{
		const TitanSkill& S = T->skills[I];
		Total += S.kind >= 0 && S.kind < static_cast<int>(C.skills.kinds.size()) && C.skills.kinds[static_cast<size_t>(S.kind)].stat == Stat ? SkillValue(S) : 0;
	}
	return Total;
}

int64_t Kingdom::TitanBonus(int32_t TitanId, RelicStat Stat) const
{
	return CapBonus(Stat, TitanRelicSum(TitanId, Stat) + TitanSkillBonus(TitanId, Stat));
}

const Building* Kingdom::Find(int32_t Id) const
{
	for (const Building& B : BuildingList)
	{
		if (B.id == Id)
		{
			return &B;
		}
	}
	return nullptr;
}

const Land* Kingdom::FindLand(int32_t Id) const
{
	for (const Land& L : LandList)
	{
		if (L.id == Id)
		{
			return &L;
		}
	}
	return nullptr;
}

// --- Spells: Spell Forge stock and Arcane Tower levels -------------------------------------------

int64_t Kingdom::SpellStock(int Spell) const
{
	return Spell >= 0 && Spell < static_cast<int>(SpellStockList.size()) ? SpellStockList[static_cast<size_t>(Spell)] : 0;
}

int Kingdom::SpellLevel(int Spell) const
{
	return Spell >= 0 && Spell < static_cast<int>(SpellLevelList.size()) ? SpellLevelList[static_cast<size_t>(Spell)] : 1;
}

int64_t Kingdom::SpellTotal() const
{
	int64_t Total = 0;
	for (const int64_t N : SpellStockList)
	{
		Total += N;
	}
	return Total;
}

int64_t Kingdom::SpellCapacity() const
{
	const LevelDef* L = BuildingLevel(C.spellForge);
	return L ? L->capacity : 0;
}

int Kingdom::CraftQueueCapacity() const
{
	const LevelDef* L = BuildingLevel(C.spellForge);
	return L ? static_cast<int>(L->queue) : 0;
}

int Kingdom::SpellResearchSlots() const
{
	const LevelDef* L = BuildingLevel(C.arcaneTower);
	return L ? static_cast<int>(L->spellSlots) : 0;
}

int Kingdom::MaxSpellLevel() const
{
	const LevelDef* L = BuildingLevel(C.arcaneTower);
	return L ? static_cast<int>(std::max<int64_t>(1, L->maxSpellLevel)) : 1;
}

int64_t Kingdom::CraftSec(int Spell) const
{
	return Shorten(C.spells[static_cast<size_t>(Spell)].craftSec, DefRelicBonus(C.spellForge, RelicStat::Craft));
}

int64_t Kingdom::CraftGold(int Spell) const
{
	return C.spells[static_cast<size_t>(Spell)].craftGold * (100 + C.spellCraftPctPerLevel * (SpellLevel(Spell) - 1)) / 100;
}

int64_t Kingdom::CraftHeadFinish() const
{
	return Craft.empty() ? 0 : CraftStart + CraftSec(Craft.front());
}

void Kingdom::ProcessCraft(int64_t Until)
{
	while (!Craft.empty() && !bCraftBlocked)
	{
		const int64_t Done = CraftStart + CraftSec(Craft.front());
		if (Done > Until)
		{
			break;
		}
		if (SpellTotal() >= SpellCapacity())
		{
			bCraftBlocked = true; // waits for room in the stock
			break;
		}
		SpellStockList[static_cast<size_t>(Craft.front())] += 1;
		Craft.erase(Craft.begin());
		CraftStart = Done;
	}
}

void Kingdom::UnblockCraft(int64_t At)
{
	if (bCraftBlocked && SpellTotal() < SpellCapacity())
	{
		bCraftBlocked = false;
		if (!Craft.empty())
		{
			CraftStart = At - CraftSec(Craft.front());
		}
		ProcessCraft(At);
	}
}

Check Kingdom::CheckCraft(int Spell) const
{
	Check Out;
	if (Spell < 0 || Spell >= static_cast<int>(C.spells.size()))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const SpellType& S = C.spells[static_cast<size_t>(Spell)];
	Out.cost[static_cast<size_t>(ResIndex(Res::Gold))] = CraftGold(Spell);
	Out.timeSec = CraftSec(Spell);
	const LevelDef* Forge = BuildingLevel(C.spellForge);
	if (!Forge || Forge->level < S.forge)
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.spellForge, S.forge, 1};
		return Out;
	}
	if (static_cast<int>(Craft.size()) >= CraftQueueCapacity())
	{
		Out.error = Error::QueueFull;
		return Out;
	}
	if (Amount(Res::Gold) < Out.cost[static_cast<size_t>(ResIndex(Res::Gold))])
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Gold))] = Out.cost[static_cast<size_t>(ResIndex(Res::Gold))] - Amount(Res::Gold);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

Error Kingdom::CraftSpell(int Spell)
{
	const Check Ck = CheckCraft(Spell);
	if (Ck.error != Error::None)
	{
		return Ck.error;
	}
	Pay(Ck.cost, false);
	if (Craft.empty())
	{
		CraftStart = Time;
		bCraftBlocked = false;
	}
	Craft.push_back(Spell);
	ProcessCraft(Time);
	NoteQuest(QuestEvent::Craft, 1);
	return Error::None;
}

Error Kingdom::CancelCraft(int Index)
{
	if (Index < 0 || Index >= static_cast<int>(Craft.size()))
	{
		return Error::BadId;
	}
	Res_[static_cast<size_t>(ResIndex(Res::Gold))] += CraftGold(Craft[static_cast<size_t>(Index)]);
	Craft.erase(Craft.begin() + Index);
	NoteQuest(QuestEvent::Craft, -1); // a cancelled spell does not count
	if (Index == 0)
	{
		CraftStart = Time;
		bCraftBlocked = false;
		ProcessCraft(Time);
	}
	return Error::None;
}

int64_t Kingdom::CraftSkipCost() const
{
	const int64_t Room = std::max<int64_t>(0, SpellCapacity() - SpellTotal());
	const size_t N = static_cast<size_t>(std::min<int64_t>(Room, static_cast<int64_t>(Craft.size())));
	int64_t Seconds = 0;
	for (size_t I = 0; I < N; ++I)
	{
		Seconds += I == 0 ? std::max<int64_t>(0, CraftHeadFinish() - Time) : CraftSec(Craft[I]);
	}
	return N == 0 ? 0 : std::max<int64_t>(1, GemsForSeconds(Seconds));
}

Error Kingdom::SkipCraft()
{
	if (Craft.empty())
	{
		return Error::Nothing;
	}
	const int64_t Room = std::max<int64_t>(0, SpellCapacity() - SpellTotal());
	if (Room <= 0)
	{
		return Error::LimitReached;
	}
	const int64_t Gems = CraftSkipCost();
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	if (Have < Gems)
	{
		return Error::NotEnough;
	}
	Have -= Gems;
	for (int64_t I = 0; I < Room && !Craft.empty(); ++I)
	{
		SpellStockList[static_cast<size_t>(Craft.front())] += 1;
		Craft.erase(Craft.begin());
	}
	CraftStart = Time;
	bCraftBlocked = false;
	ProcessCraft(Time);
	return Error::None;
}

Check Kingdom::CheckSpellUpgrade(int Spell) const
{
	Check Out;
	if (Spell < 0 || Spell >= static_cast<int>(C.spells.size()))
	{
		Out.error = Error::BadId;
		return Out;
	}
	const int Level = SpellLevel(Spell);
	int64_t Gold = C.spellGoldBase;
	int64_t Sec = C.spellSecBase;
	for (int L = 1; L < Level; ++L)
	{
		Gold = Gold * C.spellGoldGrowthPct / 100;
		Sec = Sec * C.spellSecGrowthPct / 100;
	}
	Out.cost[static_cast<size_t>(ResIndex(Res::Gold))] = Gold;
	Out.timeSec = Shorten(Sec, DefRelicBonus(C.arcaneTower, RelicStat::Research));
	const SpellType& S = C.spells[static_cast<size_t>(Spell)];
	const LevelDef* Forge = BuildingLevel(C.spellForge);
	if (!Forge || Forge->level < S.forge)
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.spellForge, S.forge, 1};
		return Out;
	}
	if (!BuildingLevel(C.arcaneTower) || Level + 1 > MaxSpellLevel())
	{
		Out.error = Error::NeedRequirement;
		Out.missing = {C.arcaneTower, 1, 1};
		if (C.arcaneTower >= 0)
		{
			for (const LevelDef& A : C.buildings[static_cast<size_t>(C.arcaneTower)].levels)
			{
				if (A.maxSpellLevel >= Level + 1)
				{
					Out.missing.level = A.level;
					break;
				}
			}
		}
		return Out;
	}
	for (const SpellResearchSlot& Lab : SpellLabs)
	{
		if (Lab.spell == Spell)
		{
			Out.error = Error::Busy;
			return Out;
		}
	}
	if (static_cast<int>(SpellLabs.size()) >= SpellResearchSlots())
	{
		Out.error = Error::Researching;
		return Out;
	}
	if (Amount(Res::Gold) < Gold)
	{
		Out.error = Error::NotEnough;
		ResArray Missing{};
		Missing[static_cast<size_t>(ResIndex(Res::Gold))] = Gold - Amount(Res::Gold);
		Out.gemsForMissing = GemsForResources(Missing);
	}
	return Out;
}

Error Kingdom::UpgradeSpell(int Spell, bool UseGems)
{
	const Check Ck = CheckSpellUpgrade(Spell);
	if (Ck.error != Error::None && !(Ck.error == Error::NotEnough && UseGems))
	{
		return Ck.error;
	}
	if (!Pay(Ck.cost, UseGems))
	{
		return Error::NotEnough;
	}
	SpellResearchSlot Lab;
	Lab.spell = Spell;
	Lab.from = Time;
	Lab.until = Time + Ck.timeSec;
	SpellLabs.push_back(Lab);
	NoteQuest(QuestEvent::Upgrade, 1);
	return Error::None;
}

int64_t Kingdom::SpellResearchSkipCost(int Index) const
{
	return Index >= 0 && Index < static_cast<int>(SpellLabs.size()) ? GemsForSeconds(std::max<int64_t>(0, SpellLabs[static_cast<size_t>(Index)].until - Time)) : 0;
}

Error Kingdom::SkipSpellResearch(int Index)
{
	if (Index < 0 || Index >= static_cast<int>(SpellLabs.size()))
	{
		return Error::NotBusy;
	}
	const int64_t Gems = SpellResearchSkipCost(Index);
	int64_t& Have = Res_[static_cast<size_t>(ResIndex(Res::Gems))];
	if (Have < Gems)
	{
		return Error::NotEnough;
	}
	Have -= Gems;
	int& Level = SpellLevelList[static_cast<size_t>(SpellLabs[static_cast<size_t>(Index)].spell)];
	Level = std::min(Level + 1, std::max(Level, MaxSpellLevel()));
	SpellLabs.erase(SpellLabs.begin() + Index);
	return Error::None;
}

// --- Relics and titans on lands -------------------------------------------------------------------

const RelicItem* Kingdom::FindRelic(int32_t Id) const
{
	for (const RelicItem& R : RelicList)
	{
		if (R.id == Id)
		{
			return &R;
		}
	}
	return nullptr;
}

RelicItem* Kingdom::MutableRelic(int32_t Id)
{
	for (RelicItem& R : RelicList)
	{
		if (R.id == Id)
		{
			return &R;
		}
	}
	return nullptr;
}

int Kingdom::RelicSlots(const TitanState& T) const
{
	int Slots = T.ascended ? C.ascendRelicSlots : 0;
	for (const int Level : C.relicSlotLevels)
	{
		Slots += T.level >= Level ? 1 : 0;
	}
	return Slots;
}

int Kingdom::RelicSlotCount(const RelicItem& R) const
{
	const RelicKind& K = C.relicKinds[static_cast<size_t>(R.kind)];
	return std::min(static_cast<int>(K.effects.size()), C.relicMaxEffects[static_cast<size_t>(std::max(1, std::min(5, R.rarity)) - 1)]);
}

int64_t Kingdom::RelicMax(const RelicItem& R) const
{
	const RelicKind& K = C.relicKinds[static_cast<size_t>(R.kind)];
	const size_t Rr = static_cast<size_t>(std::max(1, std::min(5, R.rarity)) - 1);
	if (K.economy)
	{
		return C.relicHours[Rr] * kSecondsPerHour;
	}
	return K.battles > 0 ? K.battles : C.relicDurability[Rr];
}

int64_t Kingdom::RelicLeft(const RelicItem& R) const
{
	return R.building != 0 ? std::max<int64_t>(0, R.durability - std::max<int64_t>(0, Time - R.since)) : R.durability;
}

int64_t Kingdom::RelicEffectValue(const RelicItem& R, int Slot) const
{
	if (Slot < 0 || Slot >= static_cast<int>(R.levels.size()) || R.levels[static_cast<size_t>(Slot)] <= 0)
	{
		return 0;
	}
	const RelicKind& K = C.relicKinds[static_cast<size_t>(R.kind)];
	const RelicEffectDef& E = C.relicEffects[static_cast<size_t>(K.effects[static_cast<size_t>(Slot)])];
	const int64_t Base = E.base[static_cast<size_t>(std::max(1, std::min(5, R.rarity)) - 1)];
	// Each level above the first adds a share of the first level's value (the original: up to 30-75% more).
	return Base * (100 + C.relicStepPct * (R.levels[static_cast<size_t>(Slot)] - 1)) / 100;
}

int64_t Kingdom::CapBonus(RelicStat Stat, int64_t Value) const
{
	for (const RelicEffectDef& E : C.relicEffects)
	{
		if (E.stat == Stat && E.cap > 0)
		{
			Value = std::min(Value, E.cap);
		}
	}
	return Value;
}

int32_t Kingdom::RelicTitan(int32_t RelicId) const
{
	for (const TitanState& T : TitanList)
	{
		if (std::find(T.relics.begin(), T.relics.end(), RelicId) != T.relics.end())
		{
			return T.id;
		}
	}
	return 0;
}

int64_t Kingdom::TitanRelicBonus(int32_t TitanId, RelicStat Stat) const
{
	return CapBonus(Stat, TitanRelicSum(TitanId, Stat));
}

int64_t Kingdom::TitanRelicSum(int32_t TitanId, RelicStat Stat) const
{
	const TitanState* T = FindTitan(TitanId);
	int64_t Total = 0;
	if (T)
	{
		for (const int32_t Id : T->relics)
		{
			const RelicItem* R = FindRelic(Id);
			if (R && R->durability > 0)
			{
				const RelicKind& K = C.relicKinds[static_cast<size_t>(R->kind)];
				for (int S = 0; S < static_cast<int>(R->levels.size()); ++S)
				{
					if (C.relicEffects[static_cast<size_t>(K.effects[static_cast<size_t>(S)])].stat == Stat)
					{
						Total += RelicEffectValue(*R, S);
					}
				}
			}
		}
	}
	return CapBonus(Stat, Total);
}

const RelicItem* Kingdom::BuildingRelic(int32_t BuildingId) const
{
	for (const RelicItem& R : RelicList)
	{
		if (R.building == BuildingId)
		{
			return &R;
		}
	}
	return nullptr;
}

int64_t Kingdom::BuildingRelicBonus(const Building& B, RelicStat Stat) const
{
	const RelicItem* R = BuildingRelic(B.id);
	int64_t Total = 0;
	if (R && RelicLeft(*R) > 0)
	{
		const RelicKind& K = C.relicKinds[static_cast<size_t>(R->kind)];
		for (int S = 0; S < static_cast<int>(R->levels.size()); ++S)
		{
			if (C.relicEffects[static_cast<size_t>(K.effects[static_cast<size_t>(S)])].stat == Stat)
			{
				Total += RelicEffectValue(*R, S);
			}
		}
	}
	return Total;
}

int64_t Kingdom::DefRelicBonus(int Def, RelicStat Stat) const
{
	int64_t Total = 0;
	for (const Building& B : BuildingList)
	{
		if (B.def == Def && B.level > 0)
		{
			Total += BuildingRelicBonus(B, Stat);
		}
	}
	return CapBonus(Stat, Total);
}

int Kingdom::RollRelicKind(uint64_t H) const
{
	int64_t Total = 0;
	for (const RelicKind& K : C.relicKinds)
	{
		Total += K.weight;
	}
	int64_t Roll = Total > 0 ? static_cast<int64_t>(H % static_cast<uint64_t>(Total)) : 0;
	for (size_t I = 0; I < C.relicKinds.size(); ++I)
	{
		if (Roll < C.relicKinds[I].weight)
		{
			return static_cast<int>(I);
		}
		Roll -= C.relicKinds[I].weight;
	}
	return 0;
}

int Kingdom::RollRarity(const std::array<int64_t, 5>& Weights, uint64_t H) const
{
	int64_t Total = 0;
	for (const int64_t W : Weights)
	{
		Total += W;
	}
	int64_t Roll = Total > 0 ? static_cast<int64_t>(H % static_cast<uint64_t>(Total)) : 0;
	for (int K = 0; K < 5; ++K)
	{
		if (Roll < Weights[static_cast<size_t>(K)])
		{
			return K + 1;
		}
		Roll -= Weights[static_cast<size_t>(K)];
	}
	return 1;
}

int32_t Kingdom::AddRelic(int Kind, int Rarity)
{
	if (static_cast<int>(RelicList.size()) >= C.maxRelics)
	{
		return 0;
	}
	RelicItem R;
	R.id = NextId++;
	R.kind = Kind;
	R.rarity = std::max(1, std::min(5, Rarity));
	R.durability = RelicMax(R);
	R.levels.assign(static_cast<size_t>(RelicSlotCount(R)), 0);
	for (int S = 0; S < std::min(static_cast<int>(R.levels.size()), C.relicStartEffects[static_cast<size_t>(R.rarity - 1)]); ++S)
	{
		R.levels[static_cast<size_t>(S)] = 1;
	}
	if (!R.levels.empty() && R.levels[0] == 0)
	{
		R.levels[0] = 1; // the first effect is always open
	}
	RelicList.push_back(R);
	return R.id;
}

void Kingdom::TouchRelic(RelicItem& R)
{
	if (R.building == 0)
	{
		return;
	}
	for (Building& B : BuildingList)
	{
		if (B.id == R.building)
		{
			Settle(B, Time);
		}
	}
	R.durability = RelicLeft(R);
	R.since = Time;
}

void Kingdom::UnequipEverywhere(int32_t RelicId)
{
	for (TitanState& T : TitanList)
	{
		T.relics.erase(std::remove(T.relics.begin(), T.relics.end(), RelicId), T.relics.end());
	}
	if (RelicItem* R = MutableRelic(RelicId))
	{
		TouchRelic(*R);
		R->building = 0;
		R->since = 0;
	}
}

Error Kingdom::EquipRelic(int32_t TitanId, int32_t RelicId)
{
	const RelicItem* R = FindRelic(RelicId);
	if (!R)
	{
		return Error::BadId;
	}
	if (C.relicKinds[static_cast<size_t>(R->kind)].economy)
	{
		return Error::WrongRelic;
	}
	for (TitanState& T : TitanList)
	{
		if (T.id != TitanId)
		{
			continue;
		}
		if (std::find(T.relics.begin(), T.relics.end(), RelicId) != T.relics.end())
		{
			return Error::None;
		}
		if (static_cast<int>(T.relics.size()) >= RelicSlots(T))
		{
			return Error::LimitReached;
		}
		for (const int32_t Other : T.relics)
		{
			const RelicItem* O = FindRelic(Other);
			if (O && O->kind == R->kind)
			{
				return Error::SameRelic; // the original: "Can not use identical relics"
			}
		}
		UnequipEverywhere(RelicId);
		T.relics.push_back(RelicId);
		return Error::None;
	}
	return Error::BadId;
}

Error Kingdom::AttachRelic(int32_t BuildingId, int32_t RelicId)
{
	RelicItem* R = MutableRelic(RelicId);
	const Building* B = Find(BuildingId);
	if (!R || !B)
	{
		return Error::BadId;
	}
	const RelicKind& K = C.relicKinds[static_cast<size_t>(R->kind)];
	if (!K.economy || std::find(K.buildings.begin(), K.buildings.end(), B->def) == K.buildings.end())
	{
		return Error::WrongRelic;
	}
	if (B->level <= 0)
	{
		return Error::NotReady; // still being built
	}
	if (R->building == BuildingId)
	{
		return Error::None;
	}
	if (const RelicItem* Old = BuildingRelic(BuildingId))
	{
		UnequipEverywhere(Old->id);
	}
	UnequipEverywhere(RelicId);
	for (Building& Target : BuildingList)
	{
		if (Target.id == BuildingId)
		{
			Settle(Target, Time); // it produced without the relic until now
		}
	}
	R = MutableRelic(RelicId);
	R->building = BuildingId;
	R->since = Time;
	return Error::None;
}

Error Kingdom::UnequipRelic(int32_t RelicId)
{
	if (!FindRelic(RelicId))
	{
		return Error::BadId;
	}
	UnequipEverywhere(RelicId);
	return Error::None;
}

Error Kingdom::DiscardRelic(int32_t RelicId)
{
	if (!FindRelic(RelicId))
	{
		return Error::BadId;
	}
	UnequipEverywhere(RelicId);
	RelicList.erase(std::remove_if(RelicList.begin(), RelicList.end(), [RelicId](const RelicItem& R) { return R.id == RelicId; }), RelicList.end());
	return Error::None;
}

int Kingdom::RepairChancePct(int32_t Target, const std::vector<int32_t>& Sacrifices) const
{
	const RelicItem* T = FindRelic(Target);
	if (!T)
	{
		return 0;
	}
	// The original's table: every sacrificed relic adds a chance set by its rarity and the target's.
	int64_t Sum = 0;
	for (const int32_t Id : Sacrifices)
	{
		const RelicItem* R = FindRelic(Id);
		if (R && Id != Target)
		{
			Sum += C.relicRepairPct[static_cast<size_t>(std::max(1, std::min(5, T->rarity)) - 1)][static_cast<size_t>(std::max(1, std::min(5, R->rarity)) - 1)];
		}
	}
	return static_cast<int>(std::min<int64_t>(100, Sum));
}

int Kingdom::EnhanceChancePct(int32_t Target) const
{
	const RelicItem* T = FindRelic(Target);
	return T ? static_cast<int>(C.relicEnhancePct[static_cast<size_t>(std::max(1, std::min(5, T->rarity)) - 1)]) : 0;
}

bool Kingdom::CanUnlockWith(int32_t Target, const std::vector<int32_t>& Sacrifices) const
{
	const RelicItem* T = FindRelic(Target);
	if (!T || std::find(T->levels.begin(), T->levels.end(), 0) == T->levels.end())
	{
		return false;
	}
	// The original: the second and third effects usually need higher-star relics in the repair.
	for (const int32_t Id : Sacrifices)
	{
		const RelicItem* R = FindRelic(Id);
		if (R && Id != Target && R->rarity >= T->rarity)
		{
			return true;
		}
	}
	return false;
}

Error Kingdom::RepairRelic(int32_t Target, const std::vector<int32_t>& Sacrifices, RepairResult* Out)
{
	if (Out)
	{
		*Out = RepairResult();
	}
	const RelicItem* T = FindRelic(Target);
	if (!T || Sacrifices.empty())
	{
		return Error::BadId;
	}
	if (CastleLevel() < C.relicRepairCastle)
	{
		return Error::CastleTooLow;
	}
	// A whole relic can still be repaired for the chance to improve it (the original), unless it is at its best.
	bool bCanImprove = CanUnlockWith(Target, Sacrifices);
	for (const int L : T->levels)
	{
		bCanImprove = bCanImprove || (L > 0 && L < C.relicLevels);
	}
	if (RelicLeft(*T) >= RelicMax(*T) && !bCanImprove)
	{
		return Error::Nothing;
	}
	ResArray Cost{};
	Cost[static_cast<size_t>(ResIndex(Res::Food))] = C.relicRepairFood[static_cast<size_t>(T->rarity - 1)];
	if (Amount(Res::Food) < Cost[static_cast<size_t>(ResIndex(Res::Food))])
	{
		return Error::NotEnough;
	}
	for (const int32_t Id : Sacrifices)
	{
		if (Id == Target || !FindRelic(Id) || std::count(Sacrifices.begin(), Sacrifices.end(), Id) > 1)
		{
			return Error::BadId;
		}
	}
	Pay(Cost, false);
	const int Chance = RepairChancePct(Target, Sacrifices);
	const bool bUnlock = CanUnlockWith(Target, Sacrifices);
	const uint64_t H = Mix(Seed ^ 0x726570616972ULL, RelicCounter++);
	const bool bOk = static_cast<int>(H % 100) < Chance;
	for (const int32_t Id : Sacrifices)
	{
		DiscardRelic(Id);
	}
	RepairResult Result;
	Result.repaired = bOk;
	if (bOk)
	{
		RelicItem& R = *MutableRelic(Target);
		TouchRelic(R);
		R.durability = RelicMax(R);
		// A success may also improve one effect or open the next locked one (chance by the target's rarity).
		if (static_cast<int64_t>((H >> 16) % 100) < C.relicEnhancePct[static_cast<size_t>(R.rarity - 1)])
		{
			std::vector<int> Choices;
			bool bLockedAdded = false;
			for (int S = 0; S < static_cast<int>(R.levels.size()); ++S)
			{
				const int L = R.levels[static_cast<size_t>(S)];
				if (L > 0 && L < C.relicLevels)
				{
					Choices.push_back(S);
				}
				else if (L == 0 && bUnlock && !bLockedAdded)
				{
					Choices.push_back(S);
					bLockedAdded = true;
				}
			}
			if (!Choices.empty())
			{
				const int S = Choices[static_cast<size_t>((H >> 32) % Choices.size())];
				Result.slot = S;
				Result.unlocked = R.levels[static_cast<size_t>(S)] == 0;
				R.levels[static_cast<size_t>(S)] += 1;
			}
		}
	}
	if (Out)
	{
		*Out = Result;
	}
	return Error::None;
}

void Kingdom::SettleLands()
{
	for (Land& L : LandList)
	{
		SettleLand(L, Time);
	}
}

Error Kingdom::AssignTitan(int32_t LandId, int32_t TitanId)
{
	if (!FindTitan(TitanId) || !FindLand(LandId))
	{
		return Error::BadId;
	}
	SettleLands();
	ClearGuard(TitanId); // a titan guards one place at a time
	for (Land& L : LandList)
	{
		if (L.titanId == TitanId)
		{
			L.titanId = 0;
		}
		if (L.id == LandId)
		{
			L.titanId = TitanId;
		}
	}
	return Error::None;
}

Error Kingdom::RecallTitan(int32_t LandId)
{
	for (Land& L : LandList)
	{
		if (L.id == LandId)
		{
			if (!L.titanId)
			{
				return Error::Nothing;
			}
			SettleLand(L, Time);
			L.titanId = 0;
			return Error::None;
		}
	}
	return Error::BadId;
}

// --- Save / load / hash -------------------------------------------------------------------------

std::string Kingdom::Save() const
{
	JsonWriter W;
	W.BeginObject();
	W.Key("formatVersion");
	W.Int(6);
	W.Key("contentHash");
	W.String(C.hash);
	W.Key("time");
	W.Int(Time);
	W.Key("seed");
	W.UInt(Seed);
	W.Key("worldCounter");
	W.UInt(WorldCounter);
	W.Key("nextId");
	W.Int(NextId);
	W.Key("expansions");
	W.Int(ExpansionCount);
	W.Key("extraSlots");
	W.Int(ExtraSlots);
	W.Key("stonesAt");
	W.Int(StonesAt);
	W.Key("pending");
	W.Int(Pending);
	W.Key("pendingCapture");
	W.Bool(PendingIsCapture);
	// Seasons of victory points (format 4).
	W.Key("season");
	W.BeginObject();
	W.Key("index");
	W.Int(SeasonNo);
	W.Key("last");
	W.BeginObject();
	W.Key("season");
	W.Int(Last.season);
	W.Key("vp");
	W.Int(Last.vp);
	W.Key("league");
	W.Int(Last.league);
	W.Key("gems");
	W.Int(Last.gems);
	W.Key("stones");
	W.Int(Last.stones);
	// What the alliance league paid (format 6).
	if (Last.alliance >= 0)
	{
		W.Key("alliance");
		W.Int(Last.alliance);
		W.Key("allianceTier");
		W.Int(Last.allianceTier);
		W.Key("place");
		W.Int(Last.place);
		W.Key("groupSize");
		W.Int(Last.groupSize);
		W.Key("move");
		W.Int(Last.move);
		W.Key("allianceVp");
		W.Int(Last.allianceVp);
		W.Key("crystal");
		W.Int(Last.crystal);
		W.Key("crystalTitan");
		W.Int(Last.crystalTitan);
		W.Key("crystalRelic");
		W.Int(Last.crystalRelic);
		W.Key("crystalGems");
		W.Int(Last.crystalGems);
	}
	W.EndObject();
	W.EndObject();
	// Alliances (format 6): the player's alliance, every alliance's league, an own alliance, reinforcements.
	const auto WriteSquads = [this, &W](const std::vector<Reinforcement>& List)
	{
		W.BeginArray();
		for (const Reinforcement& R : List)
		{
			W.BeginObject();
			W.Key("troop");
			W.String(C.troops[static_cast<size_t>(R.troop)].id);
			W.Key("level");
			W.Int(R.level);
			W.Key("at");
			W.Int(R.at);
			W.Key("from");
			W.Int(R.from);
			W.EndObject();
		}
		W.EndArray();
	};
	W.Key("alliance");
	W.BeginObject();
	W.Key("index");
	W.Int(HasOwnAlliance() ? -1 : AllianceIdx);
	W.Key("own");
	W.Bool(HasOwnAlliance());
	W.Key("since");
	W.Int(AllianceJoinedAt);
	W.Key("tiers");
	W.BeginArray();
	for (const int T : AllianceTiers)
	{
		W.Int(T);
	}
	W.EndArray();
	if (HasOwnAlliance())
	{
		W.Key("name");
		W.String(OwnName);
		W.Key("tag");
		W.String(OwnTag);
		W.Key("founded");
		W.Int(OwnFounded);
	}
	W.Key("reinforcements");
	WriteSquads(ReinforceList);
	W.Key("pending");
	WriteSquads(PendingReinforce);
	W.Key("askedAt");
	W.Int(ReinforceAt);
	W.Key("asked");
	W.Bool(bReinforceAsked);
	W.EndObject();
	W.Key("resources");
	W.BeginObject();
	for (int I = 0; I < kResCount; ++I)
	{
		W.Key(ToString(static_cast<Res>(I)));
		W.Int(Res_[static_cast<size_t>(I)]);
	}
	W.EndObject();
	W.Key("buildings");
	W.BeginArray();
	for (const Building& B : BuildingList)
	{
		W.BeginObject();
		W.Key("id");
		W.Int(B.id);
		W.Key("def");
		W.String(C.buildings[static_cast<size_t>(B.def)].id);
		W.Key("level");
		W.Int(B.level);
		W.Key("x");
		W.Int(B.x);
		W.Key("y");
		W.Int(B.y);
		W.Key("upgrading");
		W.Bool(B.upgrading);
		W.Key("startAt");
		W.Int(B.startAt);
		W.Key("finishAt");
		W.Int(B.finishAt);
		W.Key("stored");
		W.Int(B.stored);
		W.Key("carry");
		W.Int(B.carry);
		W.Key("lastAt");
		W.Int(B.lastAt);
		W.EndObject();
	}
	W.EndArray();
	W.Key("lands");
	W.BeginArray();
	for (const Land& L : LandList)
	{
		W.BeginObject();
		W.Key("id");
		W.Int(L.id);
		W.Key("type");
		W.String(C.landTypes[static_cast<size_t>(L.type)].id);
		W.Key("level");
		W.Int(L.level);
		W.Key("stored");
		W.Int(L.stored);
		W.Key("carry");
		W.Int(L.carry);
		W.Key("lastAt");
		W.Int(L.lastAt);
		W.Key("titan");
		W.Int(L.titanId);
		W.Key("upgrading");
		W.Bool(L.upgrading);
		W.Key("startAt");
		W.Int(L.startAt);
		W.Key("finishAt");
		W.Int(L.finishAt);
		W.Key("shield");
		W.Int(L.shieldUntil);
		W.EndObject();
	}
	W.EndArray();
	W.Key("world");
	W.BeginArray();
	for (const WorldLand& L : WorldList)
	{
		W.BeginObject();
		W.Key("id");
		W.Int(L.id);
		W.Key("type");
		W.String(C.landTypes[static_cast<size_t>(L.type)].id);
		W.Key("level");
		W.Int(L.level);
		W.EndObject();
	}
	W.EndArray();
	// Army and titans (format 2). Troops and titans are stored by id so content can grow.
	W.Key("army");
	W.BeginObject();
	W.Key("levels");
	W.BeginObject();
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		W.Key(C.troops[I].id.c_str());
		W.Int(TroopLevels[I]);
	}
	W.EndObject();
	W.Key("units");
	W.BeginObject();
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		W.Key(C.troops[I].id.c_str());
		W.Int(Army[I]);
	}
	W.EndObject();
	W.Key("queue");
	W.BeginArray();
	for (const int T : Queue)
	{
		W.String(C.troops[static_cast<size_t>(T)].id);
	}
	W.EndArray();
	W.Key("queueStart");
	W.Int(QueueStart);
	W.Key("queueBlocked");
	W.Bool(bQueueBlocked);
	W.Key("research");
	W.String(Research >= 0 ? C.troops[static_cast<size_t>(Research)].id : std::string());
	W.Key("researchFrom");
	W.Int(ResearchFrom);
	W.Key("researchUntil");
	W.Int(ResearchUntil);
	W.Key("pendingSquads");
	W.BeginArray();
	for (const int T : PendingArmy)
	{
		W.String(C.troops[static_cast<size_t>(T)].id);
	}
	W.EndArray();
	W.Key("pendingTitan");
	W.Int(PendingTitanId);
	W.EndObject();
	W.Key("titans");
	W.BeginArray();
	for (const TitanState& T : TitanList)
	{
		W.BeginObject();
		W.Key("id");
		W.Int(T.id);
		W.Key("def");
		W.String(C.titans[static_cast<size_t>(T.def)].id);
		W.Key("level");
		W.Int(T.level);
		W.Key("xp");
		W.Int(T.xp);
		W.Key("recoverUntil");
		W.Int(T.recoverUntil);
		W.Key("ascended");
		W.Bool(T.ascended);
		W.Key("relics");
		W.BeginArray();
		for (const int32_t R : T.relics)
		{
			W.Int(R);
		}
		W.EndArray();
		W.Key("skills");
		W.BeginArray();
		for (const TitanSkill& S : T.skills)
		{
			W.BeginArray();
			W.String(C.skills.kinds[static_cast<size_t>(S.kind)].id);
			W.Int(S.tier);
			W.EndArray();
		}
		W.EndArray();
		W.EndObject();
	}
	W.EndArray();
	// Spells and relic items (format 3).
	W.Key("spells");
	W.BeginObject();
	W.Key("stock");
	W.BeginObject();
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		W.Key(C.spells[I].id.c_str());
		W.Int(SpellStockList[I]);
	}
	W.EndObject();
	W.Key("levels");
	W.BeginObject();
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		W.Key(C.spells[I].id.c_str());
		W.Int(SpellLevelList[I]);
	}
	W.EndObject();
	W.Key("queue");
	W.BeginArray();
	for (const int Sp : Craft)
	{
		W.String(C.spells[static_cast<size_t>(Sp)].id);
	}
	W.EndArray();
	W.Key("queueStart");
	W.Int(CraftStart);
	W.Key("queueBlocked");
	W.Bool(bCraftBlocked);
	W.Key("research");
	W.BeginArray();
	for (const SpellResearchSlot& Lab : SpellLabs)
	{
		W.BeginObject();
		W.Key("spell");
		W.String(C.spells[static_cast<size_t>(Lab.spell)].id);
		W.Key("from");
		W.Int(Lab.from);
		W.Key("until");
		W.Int(Lab.until);
		W.EndObject();
	}
	W.EndArray();
	W.Key("pending");
	W.BeginArray();
	for (const int Sp : PendingSpellList)
	{
		W.String(C.spells[static_cast<size_t>(Sp)].id);
	}
	W.EndArray();
	W.EndObject();
	W.Key("relics");
	W.BeginArray();
	for (const RelicItem& R : RelicList)
	{
		W.BeginObject();
		W.Key("id");
		W.Int(R.id);
		W.Key("kind");
		W.String(C.relicKinds[static_cast<size_t>(R.kind)].id);
		W.Key("rarity");
		W.Int(R.rarity);
		W.Key("durability");
		W.Int(R.durability);
		// Effect levels and the building of an economy relic (format 5).
		W.Key("levels");
		W.BeginArray();
		for (const int L : R.levels)
		{
			W.Int(L);
		}
		W.EndArray();
		if (R.building != 0)
		{
			W.Key("building");
			W.Int(R.building);
			W.Key("since");
			W.Int(R.since);
		}
		W.EndObject();
	}
	W.EndArray();
	W.Key("relicAt");
	W.Int(RelicAt);
	W.Key("relicCounter");
	W.UInt(RelicCounter);
	// Raids on the kingdom (format 4, optional).
	W.Key("raids");
	W.BeginObject();
	W.Key("next");
	W.Int(NextRaid);
	W.Key("visit");
	W.Int(VisitAt);
	W.Key("counter");
	W.UInt(RaidCounter);
	W.Key("cityShield");
	W.Int(CityShield);
	W.Key("read");
	W.Int(RaidsRead);
	W.Key("garrison");
	W.BeginArray();
	for (const int T : GarrisonList)
	{
		W.String(C.troops[static_cast<size_t>(T)].id);
	}
	W.EndArray();
	W.Key("titan");
	W.Int(GarrisonTitanId);
	W.Key("log");
	W.BeginArray();
	for (const RaidReport& Rep : RaidLog)
	{
		W.BeginObject();
		W.Key("at");
		W.Int(Rep.at);
		W.Key("land");
		W.Int(Rep.landId);
		W.Key("type");
		W.String(Rep.landType >= 0 ? C.landTypes[static_cast<size_t>(Rep.landType)].id : std::string());
		W.Key("level");
		W.Int(Rep.level);
		W.Key("stars");
		W.Int(Rep.stars);
		W.Key("raid");
		W.Int(Rep.raidPower);
		W.Key("defense");
		W.Int(Rep.defensePower);
		W.Key("lost");
		W.BeginObject();
		for (int I = 0; I < kResCount; ++I)
		{
			if (Rep.lost[static_cast<size_t>(I)])
			{
				W.Key(ToString(static_cast<Res>(I)));
				W.Int(Rep.lost[static_cast<size_t>(I)]);
			}
		}
		W.EndObject();
		W.Key("vp");
		W.Int(Rep.vp);
		W.Key("titan");
		W.Int(Rep.titanId);
		W.Key("squads");
		W.Int(Rep.squads);
		W.Key("shield");
		W.Int(Rep.shieldUntil);
		W.EndObject();
	}
	W.EndArray();
	W.EndObject();
	// Titan Monuments and XP Arenas (format 6, optional): cooldowns by site id.
	W.Key("sites");
	W.BeginObject();
	W.Key("counter");
	W.UInt(SiteCounter);
	W.Key("ready");
	W.BeginObject();
	for (size_t I = 0; I < SiteReady.size() && I < C.sites.list.size(); ++I)
	{
		if (SiteReady[I] > 0)
		{
			W.Key(C.sites.list[I].id.c_str());
			W.Int(SiteReady[I]);
		}
	}
	W.EndObject();
	W.EndObject();
	// Campaigns (optional): best stars as a string of digits and opened chests as a bit mask, by act id.
	W.Key("campaigns");
	W.BeginObject();
	for (size_t A = 0; A < C.campaigns.acts.size(); ++A)
	{
		std::string Stars;
		for (int M = 0; M < MissionCount(static_cast<int>(A)); ++M)
		{
			Stars += static_cast<char>('0' + MissionStars(static_cast<int>(A), M));
		}
		const uint32_t Mask = A < ChestMask.size() ? ChestMask[A] : 0;
		if (Stars.find_first_not_of('0') == std::string::npos && Mask == 0)
		{
			continue;
		}
		W.Key(C.campaigns.acts[A].id.c_str());
		W.BeginObject();
		W.Key("stars");
		W.String(Stars.c_str());
		W.Key("chests");
		W.Int(static_cast<int64_t>(Mask));
		W.EndObject();
	}
	W.EndObject();
	// Daily quests (optional): today's quests by pool id.
	W.Key("quests");
	W.BeginObject();
	W.Key("day");
	W.Int(QuestDay);
	W.Key("rerolls");
	W.Int(QuestRerolls);
	W.Key("chest");
	W.Bool(bQuestChest);
	W.Key("counter");
	W.UInt(QuestCounter);
	W.Key("list");
	W.BeginArray();
	for (const DailyQuest& Q : QuestList)
	{
		W.BeginObject();
		W.Key("id");
		W.String(C.quests.pool[static_cast<size_t>(Q.def)].id);
		W.Key("target");
		W.Int(Q.target);
		W.Key("progress");
		W.Int(Q.progress);
		W.Key("claimed");
		W.Bool(Q.claimed);
		W.EndObject();
	}
	W.EndArray();
	W.EndObject();
	W.EndObject();
	return W.Result();
}

bool Kingdom::Load(const std::string& Text, std::string& OutError)
{
	JsonValue J;
	if (!ParseJson(Text, J, OutError))
	{
		return false;
	}
	const int Version = J.GetInt("formatVersion", 0);
	if (Version < 1 || Version > 6)
	{
		OutError = "unsupported kingdom save version";
		return false;
	}
	auto FindLandType = [this](const std::string& Id) -> int
	{
		for (size_t I = 0; I < C.landTypes.size(); ++I)
		{
			if (C.landTypes[I].id == Id)
			{
				return static_cast<int>(I);
			}
		}
		return -1;
	};
	std::vector<Building> NewBuildings;
	if (const JsonValue* List = J.Find("buildings"))
	{
		for (size_t I = 0; I < List->Size(); ++I)
		{
			const JsonValue& BJ = (*List)[I];
			Building B;
			B.id = static_cast<int32_t>(BJ.GetInt64("id", 0));
			B.def = C.FindBuilding(BJ.GetString("def"));
			if (B.def < 0)
			{
				OutError = "save refers to unknown building '" + BJ.GetString("def") + "'";
				return false;
			}
			B.level = std::min(BJ.GetInt("level", 0), C.buildings[static_cast<size_t>(B.def)].MaxLevel());
			B.x = BJ.GetInt("x", 0);
			B.y = BJ.GetInt("y", 0);
			B.upgrading = BJ.GetBool("upgrading", false) && B.level < C.buildings[static_cast<size_t>(B.def)].MaxLevel();
			B.startAt = BJ.GetInt64("startAt", 0);
			B.finishAt = BJ.GetInt64("finishAt", 0);
			B.stored = BJ.GetInt64("stored", 0);
			B.carry = BJ.GetInt64("carry", 0);
			B.lastAt = BJ.GetInt64("lastAt", 0);
			NewBuildings.push_back(B);
		}
	}
	std::vector<Land> NewLands;
	if (const JsonValue* List = J.Find("lands"))
	{
		for (size_t I = 0; I < List->Size(); ++I)
		{
			const JsonValue& LJ = (*List)[I];
			Land L;
			L.id = static_cast<int32_t>(LJ.GetInt64("id", 0));
			L.type = FindLandType(LJ.GetString("type"));
			L.level = std::max(1, LJ.GetInt("level", 1));
			L.stored = LJ.GetInt64("stored", 0);
			// Before format 4 the remainder was in units x seconds; now it is in hundredths of a unit.
			L.carry = LJ.GetInt64("carry", 0) * (Version < 4 ? 100 : 1);
			L.lastAt = LJ.GetInt64("lastAt", 0);
			L.titanId = static_cast<int32_t>(LJ.GetInt64("titan", 0));
			L.shieldUntil = LJ.GetInt64("shield", 0);
			L.startAt = LJ.GetInt64("startAt", 0);
			L.finishAt = LJ.GetInt64("finishAt", 0);
			if (L.type >= 0)
			{
				// Land upgrades are optional fields of format 4: older saves have none.
				L.upgrading = LJ.GetBool("upgrading", false) && L.level < static_cast<int>(C.landTypes[static_cast<size_t>(L.type)].produce.size());
				NewLands.push_back(L);
			}
		}
	}
	std::vector<WorldLand> NewWorld;
	if (const JsonValue* List = J.Find("world"))
	{
		for (size_t I = 0; I < List->Size(); ++I)
		{
			const JsonValue& WJ = (*List)[I];
			WorldLand W;
			W.id = static_cast<int32_t>(WJ.GetInt64("id", 0));
			W.type = FindLandType(WJ.GetString("type"));
			W.level = std::max(1, WJ.GetInt("level", 1));
			if (W.type >= 0)
			{
				NewWorld.push_back(W);
			}
		}
	}
	Time = J.GetInt64("time", 0);
	const JsonValue* SeedJ = J.Find("seed");
	Seed = SeedJ ? static_cast<uint64_t>(std::strtoull(SeedJ->text.c_str(), nullptr, 10)) : 1;
	const JsonValue* CounterJ = J.Find("worldCounter");
	WorldCounter = CounterJ ? static_cast<uint64_t>(std::strtoull(CounterJ->text.c_str(), nullptr, 10)) : 0;
	NextId = static_cast<int32_t>(J.GetInt64("nextId", 1));
	ExpansionCount = std::max(1, std::min(J.GetInt("expansions", 1), static_cast<int>(C.expansions.size())));
	ExtraSlots = std::max(0, J.GetInt("extraSlots", 0));
	StonesAt = J.GetInt64("stonesAt", Time);
	Pending = static_cast<int32_t>(J.GetInt64("pending", 0));
	PendingIsCapture = J.GetBool("pendingCapture", false);
	SeasonNo = SeasonOf(Time);
	Last = SeasonResult();
	if (const JsonValue* SeasonJ = J.Find("season"))
	{
		SeasonNo = std::min(SeasonNo, SeasonJ->GetInt64("index", SeasonNo)); // Advance pays any season that ended since
		if (const JsonValue* LastJ = SeasonJ->Find("last"))
		{
			Last.season = LastJ->GetInt64("season", -1);
			Last.vp = LastJ->GetInt64("vp", 0);
			Last.league = LastJ->GetInt("league", -1);
			Last.gems = LastJ->GetInt64("gems", 0);
			Last.stones = LastJ->GetInt64("stones", 0);
			Last.alliance = LastJ->GetInt("alliance", -1);
			Last.allianceTier = LastJ->GetInt("allianceTier", -1);
			Last.place = LastJ->GetInt("place", 0);
			Last.groupSize = LastJ->GetInt("groupSize", 0);
			Last.move = LastJ->GetInt("move", 0);
			Last.allianceVp = LastJ->GetInt64("allianceVp", 0);
			Last.crystal = LastJ->GetInt("crystal", 0);
			Last.crystalTitan = static_cast<int32_t>(LastJ->GetInt64("crystalTitan", 0));
			Last.crystalRelic = static_cast<int32_t>(LastJ->GetInt64("crystalRelic", 0));
			Last.crystalGems = LastJ->GetInt64("crystalGems", 0);
		}
	}
	Res_.fill(0);
	if (const JsonValue* R = J.Find("resources"))
	{
		for (int I = 0; I < kResCount; ++I)
		{
			Res_[static_cast<size_t>(I)] = R->GetInt64(ToString(static_cast<Res>(I)), 0);
		}
	}
	BuildingList = NewBuildings;
	LandList = NewLands;
	WorldList = NewWorld;
	const JsonValue* ArmyJ = J.Find("army");
	if (!ArmyJ)
	{
		// Format 1 saves predate the army: start it as a new kingdom would.
		ResetArmy();
	}
	else
	{
		TroopLevels.assign(C.troops.size(), 1);
		Army.assign(C.troops.size(), 0);
		for (size_t I = 0; I < C.troops.size(); ++I)
		{
			const char* Id = C.troops[I].id.c_str();
			if (const JsonValue* L = ArmyJ->Find("levels"))
			{
				TroopLevels[I] = std::max(1, std::min(L->GetInt(Id, 1), C.troops[I].MaxLevel()));
			}
			if (const JsonValue* U = ArmyJ->Find("units"))
			{
				Army[I] = std::max<int64_t>(0, U->GetInt64(Id, 0));
			}
		}
		auto ReadTroopList = [this](const JsonValue* List, std::vector<int>& Out)
		{
			Out.clear();
			for (size_t I = 0; List && I < List->Size(); ++I)
			{
				const int T = C.FindTroop((*List)[I].AsString());
				if (T >= 0)
				{
					Out.push_back(T);
				}
			}
		};
		ReadTroopList(ArmyJ->Find("queue"), Queue);
		ReadTroopList(ArmyJ->Find("pendingSquads"), PendingArmy);
		QueueStart = ArmyJ->GetInt64("queueStart", Time);
		bQueueBlocked = ArmyJ->GetBool("queueBlocked", false);
		Research = C.FindTroop(ArmyJ->GetString("research"));
		ResearchFrom = ArmyJ->GetInt64("researchFrom", 0);
		ResearchUntil = ArmyJ->GetInt64("researchUntil", 0);
		PendingTitanId = static_cast<int32_t>(ArmyJ->GetInt64("pendingTitan", 0));
		TitanList.clear();
		if (const JsonValue* List = J.Find("titans"))
		{
			for (size_t I = 0; I < List->Size(); ++I)
			{
				const JsonValue& TJ = (*List)[I];
				TitanState T;
				T.id = static_cast<int32_t>(TJ.GetInt64("id", 0));
				T.def = C.FindTitan(TJ.GetString("def"));
				if (T.def < 0)
				{
					continue;
				}
				T.ascended = TJ.GetBool("ascended", false);
				T.level = std::max(1, std::min(TJ.GetInt("level", 1), TitanMaxLevel(T)));
				T.xp = std::max<int64_t>(0, TJ.GetInt64("xp", 0));
				T.recoverUntil = TJ.GetInt64("recoverUntil", 0);
				if (const JsonValue* Rl = TJ.Find("relics"))
				{
					for (size_t K = 0; K < Rl->Size(); ++K)
					{
						T.relics.push_back(static_cast<int32_t>((*Rl)[K].AsInt64(0)));
					}
				}
				// Skills (saves before them get the start skill, and the ascension one, from the seed); kinds the content
				// no longer has are dropped.
				if (const JsonValue* Sk = TJ.Find("skills"))
				{
					for (size_t K = 0; K < Sk->Size() && static_cast<int>(T.skills.size()) < SkillSlots(T); ++K)
					{
						const JsonValue& SJ = (*Sk)[K];
						for (size_t D = 0; SJ.Size() == 2 && D < C.skills.kinds.size(); ++D)
						{
							if (C.skills.kinds[D].id == SJ[0].AsString())
							{
								TitanSkill S;
								S.kind = static_cast<int>(D);
								S.tier = std::max(1, std::min(3, static_cast<int>(SJ[1].AsInt64(1))));
								T.skills.push_back(S);
							}
						}
					}
				}
				else
				{
					StartSkills(T);
				}
				TitanList.push_back(T);
			}
		}
		RelicAt = J.GetInt64("relicAt", Time);
		const JsonValue* RelicJ = J.Find("relicCounter");
		RelicCounter = RelicJ ? static_cast<uint64_t>(std::strtoull(RelicJ->text.c_str(), nullptr, 10)) : 0;
	}
	const JsonValue* SpellsJ = J.Find("spells");
	if (!SpellsJ || !SpellsJ->IsObject())
	{
		// Saves before format 3 have no spells or relic items.
		std::vector<int32_t> Guards;
		for (const Land& L : LandList)
		{
			Guards.push_back(L.titanId);
		}
		ResetSpellsAndRelics();
		for (size_t I = 0; I < LandList.size(); ++I)
		{
			LandList[I].titanId = Guards[I];
		}
	}
	else
	{
		SpellStockList.assign(C.spells.size(), 0);
		SpellLevelList.assign(C.spells.size(), 1);
		for (size_t I = 0; I < C.spells.size(); ++I)
		{
			const char* Id = C.spells[I].id.c_str();
			if (const JsonValue* St = SpellsJ->Find("stock"))
			{
				SpellStockList[I] = std::max<int64_t>(0, St->GetInt64(Id, 0));
			}
			if (const JsonValue* Lv = SpellsJ->Find("levels"))
			{
				SpellLevelList[I] = std::max(1, Lv->GetInt(Id, 1));
			}
		}
		auto ReadSpellList = [this](const JsonValue* List, std::vector<int>& Out)
		{
			Out.clear();
			for (size_t I = 0; List && I < List->Size(); ++I)
			{
				const int Sp = C.FindSpell((*List)[I].AsString());
				if (Sp >= 0)
				{
					Out.push_back(Sp);
				}
			}
		};
		ReadSpellList(SpellsJ->Find("queue"), Craft);
		ReadSpellList(SpellsJ->Find("pending"), PendingSpellList);
		CraftStart = SpellsJ->GetInt64("queueStart", Time);
		bCraftBlocked = SpellsJ->GetBool("queueBlocked", false);
		SpellLabs.clear();
		if (const JsonValue* Labs = SpellsJ->Find("research"))
		{
			for (size_t I = 0; I < Labs->Size(); ++I)
			{
				SpellResearchSlot Lab;
				Lab.spell = C.FindSpell((*Labs)[I].GetString("spell"));
				Lab.from = (*Labs)[I].GetInt64("from", 0);
				Lab.until = (*Labs)[I].GetInt64("until", 0);
				if (Lab.spell >= 0)
				{
					SpellLabs.push_back(Lab);
				}
			}
		}
		RelicList.clear();
		if (const JsonValue* List = J.Find("relics"))
		{
			for (size_t I = 0; I < List->Size(); ++I)
			{
				const JsonValue& RJ = (*List)[I];
				RelicItem R;
				R.id = static_cast<int32_t>(RJ.GetInt64("id", 0));
				R.kind = C.FindRelicKind(RJ.GetString("kind"));
				R.rarity = std::max(1, std::min(5, RJ.GetInt("rarity", 1)));
				if (R.kind < 0)
				{
					continue;
				}
				R.durability = std::max<int64_t>(0, std::min(RelicMax(R), RJ.GetInt64("durability", 0)));
				// Relics from before format 5 get the effects a new relic of their rarity has.
				R.levels.assign(static_cast<size_t>(RelicSlotCount(R)), 0);
				const JsonValue* Levels = RJ.Find("levels");
				for (int S = 0; S < static_cast<int>(R.levels.size()); ++S)
				{
					const int Start = S == 0 || S < C.relicStartEffects[static_cast<size_t>(R.rarity - 1)] ? 1 : 0;
					R.levels[static_cast<size_t>(S)] = Levels ? std::max(0, std::min(C.relicLevels, (S < static_cast<int>(Levels->Size()) ? (*Levels)[static_cast<size_t>(S)].AsInt(0) : 0))) : Start;
				}
				if (!R.levels.empty())
				{
					R.levels[0] = std::max(1, R.levels[0]);
				}
				R.building = static_cast<int32_t>(RJ.GetInt64("building", 0));
				R.since = R.building ? RJ.GetInt64("since", Time) : 0;
				RelicList.push_back(R);
			}
		}
	}
	// Drop references to relics or titans that are gone, and relics on the wrong titan or building.
	for (TitanState& T : TitanList)
	{
		T.relics.erase(std::remove_if(T.relics.begin(), T.relics.end(), [this](int32_t Id)
			{
				const RelicItem* R = FindRelic(Id);
				return !R || C.relicKinds[static_cast<size_t>(R->kind)].economy;
			}), T.relics.end());
	}
	for (size_t I = 0; I < RelicList.size(); ++I)
	{
		RelicItem& R = RelicList[I];
		const Building* B = R.building ? Find(R.building) : nullptr;
		const RelicKind& K = C.relicKinds[static_cast<size_t>(R.kind)];
		bool bTaken = false;
		for (size_t Prev = 0; Prev < I; ++Prev)
		{
			bTaken = bTaken || RelicList[Prev].building == R.building;
		}
		if (R.building && (!B || !K.economy || bTaken || std::find(K.buildings.begin(), K.buildings.end(), B->def) == K.buildings.end()))
		{
			R.building = 0;
			R.since = 0;
		}
	}
	for (Land& L : LandList)
	{
		if (L.titanId && !FindTitan(L.titanId))
		{
			L.titanId = 0;
		}
	}
	// Alliances (format 6). Older saves start outside any alliance, every alliance in the league it starts in.
	ResetAlliances();
	if (const JsonValue* AJ = J.Find("alliance"))
	{
		const int Base = BaseAlliances();
		const int Tiers = static_cast<int>(C.alliances.leagues.size());
		bool bOwn = AJ->GetBool("own", false);
		std::vector<int> Saved;
		if (const JsonValue* TJ = AJ->Find("tiers"))
		{
			for (size_t I = 0; I < TJ->Size(); ++I)
			{
				Saved.push_back(std::max(0, std::min(std::max(0, Tiers - 1), (*TJ)[I].AsInt(0))));
			}
		}
		// A save from content with another number of alliances keeps nothing but the player's own alliance.
		const bool bSameWorld = static_cast<int>(Saved.size()) == Base + (bOwn ? 1 : 0);
		if (bSameWorld)
		{
			AllianceTiers = Saved;
		}
		if (bOwn)
		{
			bOwn = CleanName(AJ->GetString("name"), kMaxNameBytes, OwnName) && CleanName(AJ->GetString("tag"), kMaxTagBytes, OwnTag);
			if (bOwn)
			{
				AllianceTiers.resize(static_cast<size_t>(Base), 0);
				AllianceTiers.push_back(bSameWorld ? Saved.back() : 0);
				OwnFounded = AJ->GetInt64("founded", Time);
				AllianceIdx = Base;
			}
			else
			{
				AllianceTiers.resize(static_cast<size_t>(Base), 0);
				OwnName.clear();
				OwnTag.clear();
			}
		}
		else
		{
			const int Idx = AJ->GetInt("index", -1);
			AllianceIdx = Idx >= 0 && Idx < Base ? Idx : -1;
		}
		if (AllianceIdx >= 0)
		{
			AllianceJoinedAt = AJ->GetInt64("since", Time);
		}
		const auto ReadSquads = [this](const JsonValue* List, std::vector<Reinforcement>& Into)
		{
			Into.clear();
			for (size_t I = 0; List && I < List->Size(); ++I)
			{
				const JsonValue& RJ = (*List)[I];
				Reinforcement R;
				R.troop = C.FindTroop(RJ.GetString("troop"));
				if (R.troop < 0)
				{
					continue;
				}
				R.level = std::max(1, std::min(C.troops[static_cast<size_t>(R.troop)].MaxLevel(), RJ.GetInt("level", 1)));
				R.at = RJ.GetInt64("at", Time);
				R.from = std::max(0, RJ.GetInt("from", 0));
				Into.push_back(R);
			}
		};
		if (AllianceIdx >= 0)
		{
			ReadSquads(AJ->Find("reinforcements"), ReinforceList);
		}
		if (Pending != 0)
		{
			ReadSquads(AJ->Find("pending"), PendingReinforce);
		}
		ReinforceAt = AJ->GetInt64("askedAt", 0);
		bReinforceAsked = AJ->GetBool("asked", false);
	}
	// Raids (saves before them start like a new kingdom: the first raid comes a day after loading).
	ResetRaids(Time);
	if (const JsonValue* RaidsJ = J.Find("raids"))
	{
		NextRaid = RaidsJ->GetInt64("next", NextRaid);
		VisitAt = RaidsJ->GetInt64("visit", Time);
		const JsonValue* CounterJ2 = RaidsJ->Find("counter");
		RaidCounter = CounterJ2 ? static_cast<uint64_t>(std::strtoull(CounterJ2->text.c_str(), nullptr, 10)) : 0;
		CityShield = RaidsJ->GetInt64("cityShield", 0);
		RaidsRead = RaidsJ->GetInt64("read", Time);
		if (const JsonValue* List = RaidsJ->Find("garrison"))
		{
			for (size_t I = 0; I < List->Size(); ++I)
			{
				const int T = C.FindTroop((*List)[I].AsString());
				if (T >= 0)
				{
					GarrisonList.push_back(T);
				}
			}
		}
		GarrisonTitanId = static_cast<int32_t>(RaidsJ->GetInt64("titan", 0));
		if (const JsonValue* List = RaidsJ->Find("log"))
		{
			for (size_t I = 0; I < List->Size(); ++I)
			{
				const JsonValue& RJ = (*List)[I];
				RaidReport Rep;
				Rep.at = RJ.GetInt64("at", 0);
				Rep.landId = static_cast<int32_t>(RJ.GetInt64("land", 0));
				Rep.landType = Rep.landId ? FindLandType(RJ.GetString("type")) : -1;
				Rep.level = std::max(1, RJ.GetInt("level", 1));
				Rep.stars = std::max(0, std::min(3, RJ.GetInt("stars", 0)));
				Rep.raidPower = RJ.GetInt64("raid", 0);
				Rep.defensePower = RJ.GetInt64("defense", 0);
				if (const JsonValue* Lost = RJ.Find("lost"))
				{
					for (int K = 0; K < kResCount; ++K)
					{
						Rep.lost[static_cast<size_t>(K)] = Lost->GetInt64(ToString(static_cast<Res>(K)), 0);
					}
				}
				Rep.vp = RJ.GetInt64("vp", 0);
				Rep.titanId = static_cast<int32_t>(RJ.GetInt64("titan", 0));
				Rep.squads = RJ.GetInt("squads", 0);
				Rep.shieldUntil = RJ.GetInt64("shield", 0);
				RaidLog.push_back(Rep);
			}
		}
	}
	if (GarrisonTitanId && !FindTitan(GarrisonTitanId))
	{
		GarrisonTitanId = 0;
	}
	// Titan Monuments and XP Arenas (saves before them start with every site open).
	SiteReady.assign(C.sites.list.size(), 0);
	SiteCounter = 0;
	if (const JsonValue* SitesJ = J.Find("sites"))
	{
		const JsonValue* CounterJ3 = SitesJ->Find("counter");
		SiteCounter = CounterJ3 ? static_cast<uint64_t>(std::strtoull(CounterJ3->text.c_str(), nullptr, 10)) : 0;
		if (const JsonValue* ReadyJ = SitesJ->Find("ready"))
		{
			for (size_t I = 0; I < C.sites.list.size(); ++I)
			{
				SiteReady[I] = std::max<int64_t>(0, ReadyJ->GetInt64(C.sites.list[I].id.c_str(), 0));
			}
		}
	}
	// Campaigns (saves before them have played no mission).
	MissionStarList.assign(C.campaigns.acts.size(), {});
	ChestMask.assign(C.campaigns.acts.size(), 0);
	if (const JsonValue* CampJ = J.Find("campaigns"))
	{
		for (size_t A = 0; A < C.campaigns.acts.size(); ++A)
		{
			const JsonValue* ActJ = CampJ->Find(C.campaigns.acts[A].id.c_str());
			if (!ActJ)
			{
				continue;
			}
			const std::string Stars = ActJ->GetString("stars");
			std::vector<uint8_t>& Best = MissionStarList[A];
			Best.assign(C.campaigns.acts[A].missions.size(), 0);
			for (size_t M = 0; M < Best.size() && M < Stars.size(); ++M)
			{
				Best[M] = static_cast<uint8_t>(Stars[M] >= '1' && Stars[M] <= '3' ? Stars[M] - '0' : 0);
			}
			ChestMask[A] = static_cast<uint32_t>(std::max<int64_t>(0, ActJ->GetInt64("chests", 0)));
		}
	}
	// Daily quests (saves before them roll today's quests on the next Advance); quests the pool no longer has are dropped.
	QuestDay = -1;
	QuestList.clear();
	QuestRerolls = 0;
	bQuestChest = false;
	QuestCounter = 0;
	if (const JsonValue* QuestsJ = J.Find("quests"))
	{
		QuestDay = QuestsJ->GetInt64("day", -1);
		QuestRerolls = std::max(0, QuestsJ->GetInt("rerolls", 0));
		bQuestChest = QuestsJ->GetBool("chest", false);
		const JsonValue* CounterJ4 = QuestsJ->Find("counter");
		QuestCounter = CounterJ4 ? static_cast<uint64_t>(std::strtoull(CounterJ4->text.c_str(), nullptr, 10)) : 0;
		if (const JsonValue* ListJ = QuestsJ->Find("list"))
		{
			for (size_t I = 0; I < ListJ->Size(); ++I)
			{
				const JsonValue& QJ = (*ListJ)[I];
				const std::string Id = QJ.GetString("id");
				for (size_t D = 0; D < C.quests.pool.size(); ++D)
				{
					if (C.quests.pool[D].id == Id)
					{
						DailyQuest Q;
						Q.def = static_cast<int>(D);
						Q.target = std::max<int64_t>(1, QJ.GetInt64("target", 1));
						Q.progress = std::max<int64_t>(0, QJ.GetInt64("progress", 0));
						Q.claimed = QJ.GetBool("claimed", false);
						QuestList.push_back(Q);
					}
				}
			}
		}
	}
	if (Pending < 0)
	{
		// A site or mission the content no longer has.
		const int Mission = PendingMission();
		if (Mission >= 0 ? Mission % 100 >= MissionCount(Mission / 100) : -Pending > static_cast<int32_t>(C.sites.list.size()))
		{
			Pending = 0;
		}
	}
	FillWorld();
	return true;
}

uint64_t Kingdom::Hash() const
{
	StateWriter W;
	W.I64(Time);
	for (const int64_t V : Res_)
	{
		W.I64(V);
	}
	W.I64(StonesAt);
	W.I32(ExpansionCount);
	W.I32(ExtraSlots);
	W.I32(NextId);
	W.I32(Pending);
	W.I64(SeasonNo);
	W.I64(Last.season);
	W.I64(Last.vp);
	W.I32(Last.league);
	W.I64(Last.gems);
	W.I64(Last.stones);
	W.I32(Last.alliance);
	W.I32(Last.allianceTier);
	W.I32(Last.place);
	W.I32(Last.groupSize);
	W.I32(Last.move);
	W.I64(Last.allianceVp);
	W.I32(Last.crystal);
	W.I32(Last.crystalTitan);
	W.I32(Last.crystalRelic);
	W.I64(Last.crystalGems);
	W.I32(AllianceIdx);
	W.I64(AllianceJoinedAt);
	for (const int T : AllianceTiers)
	{
		W.I32(T);
	}
	for (const std::string* Text : {&OwnName, &OwnTag})
	{
		W.I32(static_cast<int32_t>(Text->size()));
		for (const char Ch : *Text)
		{
			W.U8(static_cast<uint8_t>(Ch));
		}
	}
	W.I64(OwnFounded);
	for (const std::vector<Reinforcement>* List : {&ReinforceList, &PendingReinforce})
	{
		W.I32(static_cast<int32_t>(List->size()));
		for (const Reinforcement& R : *List)
		{
			W.I32(R.troop);
			W.I32(R.level);
			W.I64(R.at);
			W.I32(R.from);
		}
	}
	W.I64(ReinforceAt);
	W.Bool(bReinforceAsked);
	for (const Building& B : BuildingList)
	{
		W.I32(B.id);
		W.I32(B.def);
		W.I32(B.level);
		W.I32(B.x);
		W.I32(B.y);
		W.Bool(B.upgrading);
		W.I64(B.startAt);
		W.I64(B.finishAt);
		W.I64(B.stored);
		W.I64(B.carry);
		W.I64(B.lastAt);
	}
	for (const Land& L : LandList)
	{
		W.I32(L.id);
		W.I32(L.type);
		W.I32(L.level);
		W.I64(L.stored);
		W.I64(L.carry);
		W.I64(L.lastAt);
	}
	for (const WorldLand& L : WorldList)
	{
		W.I32(L.id);
		W.I32(L.type);
		W.I32(L.level);
	}
	for (size_t I = 0; I < TroopLevels.size(); ++I)
	{
		W.I32(TroopLevels[I]);
		W.I64(Army[I]);
	}
	for (const int T : Queue)
	{
		W.I32(T);
	}
	W.I64(QueueStart);
	W.Bool(bQueueBlocked);
	W.I32(Research);
	W.I64(ResearchFrom);
	W.I64(ResearchUntil);
	for (const int T : PendingArmy)
	{
		W.I32(T);
	}
	W.I32(PendingTitanId);
	for (const TitanState& T : TitanList)
	{
		W.I32(T.id);
		W.I32(T.def);
		W.I32(T.level);
		W.I64(T.xp);
		W.I64(T.recoverUntil);
		W.Bool(T.ascended);
	}
	W.I64(RelicAt);
	W.U64(RelicCounter);
	for (const Land& L : LandList)
	{
		W.I32(L.titanId);
	}
	for (const TitanState& T : TitanList)
	{
		for (const int32_t R : T.relics)
		{
			W.I32(R);
		}
		W.U32(static_cast<uint32_t>(T.skills.size()));
		for (const TitanSkill& S : T.skills)
		{
			W.I32(S.kind);
			W.I32(S.tier);
		}
	}
	for (size_t I = 0; I < SpellStockList.size(); ++I)
	{
		W.I64(SpellStockList[I]);
		W.I32(SpellLevelList[I]);
	}
	for (const int Sp : Craft)
	{
		W.I32(Sp);
	}
	W.I64(CraftStart);
	W.Bool(bCraftBlocked);
	for (const SpellResearchSlot& Lab : SpellLabs)
	{
		W.I32(Lab.spell);
		W.I64(Lab.from);
		W.I64(Lab.until);
	}
	for (const int Sp : PendingSpellList)
	{
		W.I32(Sp);
	}
	for (const RelicItem& R : RelicList)
	{
		W.I32(R.id);
		W.I32(R.kind);
		W.I32(R.rarity);
		W.I64(R.durability);
		for (const int L : R.levels)
		{
			W.I32(L);
		}
		W.I32(R.building);
		W.I64(R.since);
	}
	// Land upgrades (only lands being upgraded, so states without them hash as before).
	for (const Land& L : LandList)
	{
		if (L.upgrading)
		{
			W.I32(L.id);
			W.I64(L.startAt);
			W.I64(L.finishAt);
		}
	}
	// Raids
	W.I64(NextRaid);
	W.I64(VisitAt);
	W.U64(RaidCounter);
	W.I64(CityShield);
	W.I64(RaidsRead);
	for (const int T : GarrisonList)
	{
		W.I32(T);
	}
	W.I32(GarrisonTitanId);
	for (const Land& L : LandList)
	{
		W.I64(L.shieldUntil);
	}
	for (const RaidReport& Rep : RaidLog)
	{
		W.I64(Rep.at);
		W.I32(Rep.landId);
		W.I32(Rep.level);
		W.I32(Rep.stars);
		for (const int64_t V : Rep.lost)
		{
			W.I64(V);
		}
		W.I64(Rep.vp);
	}
	// Sites (only once one has been fought, so states without them hash as before).
	if (SiteCounter != 0 || std::any_of(SiteReady.begin(), SiteReady.end(), [](int64_t T) { return T != 0; }))
	{
		W.U64(SiteCounter);
		for (const int64_t T : SiteReady)
		{
			W.I64(T);
		}
	}
	// Campaigns (only once a mission is won or a chest opened).
	for (size_t A = 0; A < MissionStarList.size() || A < ChestMask.size(); ++A)
	{
		const int Stars = ActStars(static_cast<int>(A));
		const uint32_t Mask = A < ChestMask.size() ? ChestMask[A] : 0;
		if (Stars == 0 && Mask == 0)
		{
			continue;
		}
		W.I32(static_cast<int32_t>(A));
		for (int M = 0; M < MissionCount(static_cast<int>(A)); ++M)
		{
			W.I32(MissionStars(static_cast<int>(A), M));
		}
		W.U32(Mask);
	}
	W.I64(QuestDay);
	W.I32(QuestRerolls);
	W.U32(bQuestChest ? 1u : 0u);
	W.U64(QuestCounter);
	for (const DailyQuest& Q : QuestList)
	{
		W.I32(Q.def);
		W.I64(Q.target);
		W.I64(Q.progress);
		W.U32(Q.claimed ? 1u : 0u);
	}
	return W.Hash();
}
} // namespace kingdom
} // namespace dot
