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
} // namespace

const char* ToString(Error E)
{
	static const char* const Names[] = {"none", "bad_id", "max_level", "castle_too_low", "need_requirement", "need_expansion", "locked",
		"limit_reached", "no_builder", "not_enough", "bad_placement", "busy", "not_busy", "nothing", "storage_full", "need_stones",
		"no_land_slot", "no_attack", "queue_full", "researching", "too_many_squads", "no_troops", "titan_recovering", "not_ready",
		"roster_full", "not_max_level"};
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
	return (D.role == Role::Producer && L && !B.upgrading) ? L->produce : 0;
}

int64_t Kingdom::ProducerCap(const Building& B) const
{
	const LevelDef* L = CurrentLevel(B);
	return L ? L->store : 0;
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
		B.stored = std::min(Cap, B.stored + Gained / kSecondsPerHour);
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
		const int64_t TR = bResearch ? ResearchUntil : InfT;
		const int64_t TS = Lab >= 0 ? SpellLabs[static_cast<size_t>(Lab)].until : InfT;
		// The end of a season is an event too: its league reward may add portal stones.
		const int64_t TN = SeasonOf(Now) > SeasonNo ? std::max(Time, SeasonStart(SeasonNo + 1)) : InfT;
		if (TB == InfT && TR == InfT && TS == InfT && TN == InfT)
		{
			break;
		}
		const int64_t At = std::min(std::min(TB, TR), std::min(TS, TN));
		ProcessQueue(At);
		ProcessCraft(At);
		RegenStones(At);
		if (TN == At)
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
		else
		{
			Complete(*NextB, At);
			UnblockQueue(At); // a finished Army Camp may have room now
			UnblockCraft(At); // and a finished Spell Forge
		}
	}
	ProcessQueue(Now);
	ProcessCraft(Now);
	RegenStones(Now);
	Time = Now;
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
			LandList.erase(LandList.begin() + static_cast<std::ptrdiff_t>(I));
			return Error::None;
		}
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
	if (Vp > 0)
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
	const int MaxLevel = static_cast<int>(C.raidLoot.size());
	const int Lo = std::max(1, std::min(MaxLevel, (Castle + 1) / 3));
	const int Hi = std::max(Lo, std::min(MaxLevel, Lo + 2));
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
	const int64_t Cost = Capture ? C.captureStones : C.raidStones;
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
	Pending = WorldId;
	PendingIsCapture = Capture;
	PendingArmy = Squads;
	PendingTitanId = TitanId;
	PendingSpellList = Spells;
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

Error Kingdom::FinishAttack(int32_t WorldId, bool Capture, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out)
{
	if (Pending == 0 || Pending != WorldId || PendingIsCapture != Capture)
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
			const int LandLevel = Index < WorldList.size() ? WorldList[Index].level : 1;
			const int64_t Base = C.titanBattleXp[static_cast<size_t>(std::min<int>(LandLevel, static_cast<int>(C.titanBattleXp.size())) - 1)];
			R.titanId = T.id;
			R.titanXp = Base * C.titanStarsPct[static_cast<size_t>(std::max(0, std::min(3, Stars)))] / 100;
			AddTitanXp(T, R.titanXp, &R.titanLevels);
			if (T.level > C.titanRecoveryFromLevel)
			{
				T.recoverUntil = Time + (T.level - C.titanRecoveryFromLevel) * C.titanRecoverySecPerLevel;
			}
		}
	}
	PendingArmy.clear();
	PendingTitanId = 0;
	UnblockQueue(Time);
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

int Kingdom::SurvivalPct() const
{
	const LevelDef* L = BuildingLevel(C.medicalSchool);
	return L && L->survival > 0 ? static_cast<int>(L->survival) : 100;
}

int64_t Kingdom::TrainSec(int Troop) const
{
	const TroopType& T = C.troops[static_cast<size_t>(Troop)];
	return T.levels[static_cast<size_t>(std::max(1, std::min(TroopLevel(Troop), T.MaxLevel())) - 1)].trainSec;
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
	Out.timeSec = L.trainSec;
	if (CastleLevel() < T.unlockCastle)
	{
		Out.error = Error::Locked;
		Out.castle = T.unlockCastle;
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
	Out.timeSec = L.upgradeSec;
	if (CastleLevel() < T.unlockCastle)
	{
		Out.error = Error::Locked;
		Out.castle = T.unlockCastle;
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
	if (static_cast<int>(TitanList.size()) >= TitanCapacity())
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
	// The free daily relic is either a relic item or a titan (the original's daily bonus).
	if (A.cooldownSec > 0 && static_cast<int64_t>((H >> 40) % 100) < C.dailyRelicPct && static_cast<int>(RelicList.size()) < C.maxRelics)
	{
		int64_t Total = 0;
		for (const int64_t W : C.dailyRelicRarity)
		{
			Total += W;
		}
		int64_t Roll = Total > 0 ? static_cast<int64_t>((H >> 8) % static_cast<uint64_t>(Total)) : 0;
		RelicItem Rl;
		Rl.id = NextId++;
		Rl.kind = static_cast<int>((H >> 52) % static_cast<uint64_t>(C.relicKinds.size()));
		for (int K = 0; K < 5; ++K)
		{
			if (Roll < C.dailyRelicRarity[static_cast<size_t>(K)])
			{
				Rl.rarity = K + 1;
				break;
			}
			Roll -= C.dailyRelicRarity[static_cast<size_t>(K)];
		}
		Rl.durability = static_cast<int>(C.relicDurability[static_cast<size_t>(Rl.rarity - 1)]);
		RelicList.push_back(Rl);
		RelicAt = Time + A.cooldownSec;
		if (OutRelicId)
		{
			*OutRelicId = Rl.id;
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
	T.def = Pool[static_cast<size_t>((H >> 24) % static_cast<uint64_t>(Pool.size()))];
	TitanList.push_back(T);
	if (A.cooldownSec > 0)
	{
		RelicAt = Time + A.cooldownSec;
	}
	if (OutTitanId)
	{
		*OutTitanId = T.id;
	}
	return Error::None;
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
	int64_t Xp = 0;
	for (const int32_t Id : Sacrifices)
	{
		const TitanState* S = FindTitan(Id);
		const int64_t Stars = C.titans[static_cast<size_t>(S->def)].stars;
		Xp += C.fusionXpPerStarSq * Stars * Stars + TitanTotalXp(*S) * C.fusionKeepXpPct / 100;
	}
	TitanList.erase(std::remove_if(TitanList.begin(), TitanList.end(),
		[&Sacrifices](const TitanState& T) { return std::find(Sacrifices.begin(), Sacrifices.end(), T.id) != Sacrifices.end(); }), TitanList.end());
	for (TitanState& T : TitanList)
	{
		if (T.id == Target)
		{
			AddTitanXp(T, Xp, nullptr);
		}
	}
	return Error::None;
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
	return C.spells[static_cast<size_t>(Spell)].craftSec;
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
	Out.timeSec = S.craftSec;
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
	Out.timeSec = Sec;
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

int Kingdom::RelicSlots(const TitanState& T) const
{
	int Slots = T.ascended ? C.ascendRelicSlots : 0;
	for (const int Level : C.relicSlotLevels)
	{
		Slots += T.level >= Level ? 1 : 0;
	}
	return Slots;
}

int64_t Kingdom::RelicStatPct(const RelicItem& R, int Stat) const
{
	const int64_t Main = C.relicBonusPct[static_cast<size_t>(std::max(1, std::min(5, R.rarity)) - 1)];
	const int MainStat = C.relicKinds[static_cast<size_t>(R.kind)].stat;
	if (Stat == MainStat)
	{
		return Main;
	}
	// From rarity 3 a relic also carries half its bonus on the next stat.
	return R.rarity >= 3 && Stat == (MainStat + 1) % 3 ? Main / 2 : 0;
}

int64_t Kingdom::TitanBonusPct(int32_t TitanId, int Stat) const
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
				Total += RelicStatPct(*R, Stat);
			}
		}
	}
	return Total;
}

void Kingdom::UnequipEverywhere(int32_t RelicId)
{
	for (TitanState& T : TitanList)
	{
		T.relics.erase(std::remove(T.relics.begin(), T.relics.end(), RelicId), T.relics.end());
	}
}

Error Kingdom::EquipRelic(int32_t TitanId, int32_t RelicId)
{
	if (!FindRelic(RelicId))
	{
		return Error::BadId;
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
		UnequipEverywhere(RelicId);
		T.relics.push_back(RelicId);
		return Error::None;
	}
	return Error::BadId;
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
	int64_t Sum = 0;
	for (const int32_t Id : Sacrifices)
	{
		const RelicItem* R = FindRelic(Id);
		if (R && Id != Target)
		{
			Sum += C.relicRepairWeight[static_cast<size_t>(std::max(1, std::min(5, R->rarity)) - 1)];
		}
	}
	// Two relics of the target's rarity always do it; lower rarities add less.
	const int64_t Need = 2 * C.relicRepairWeight[static_cast<size_t>(std::max(1, std::min(5, T->rarity)) - 1)];
	return static_cast<int>(std::min<int64_t>(100, Need > 0 ? Sum * 100 / Need : 100));
}

Error Kingdom::RepairRelic(int32_t Target, const std::vector<int32_t>& Sacrifices, bool* OutRepaired)
{
	if (OutRepaired)
	{
		*OutRepaired = false;
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
	if (T->durability >= C.relicDurability[static_cast<size_t>(T->rarity - 1)])
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
	const uint64_t H = Mix(Seed ^ 0x726570616972ULL, RelicCounter++);
	const bool bOk = static_cast<int>(H % 100) < Chance;
	for (const int32_t Id : Sacrifices)
	{
		DiscardRelic(Id);
	}
	if (bOk)
	{
		for (RelicItem& R : RelicList)
		{
			if (R.id == Target)
			{
				R.durability = static_cast<int>(C.relicDurability[static_cast<size_t>(R.rarity - 1)]);
			}
		}
	}
	if (OutRepaired)
	{
		*OutRepaired = bOk;
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
	for (Land& L : LandList)
	{
		if (L.titanId == TitanId)
		{
			L.titanId = 0; // a titan guards one land at a time
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
	W.Int(4);
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
	W.EndObject();
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
		W.EndObject();
	}
	W.EndArray();
	W.Key("relicAt");
	W.Int(RelicAt);
	W.Key("relicCounter");
	W.UInt(RelicCounter);
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
	if (Version < 1 || Version > 4)
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
			if (L.type >= 0)
			{
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
				R.kind = -1;
				for (size_t K = 0; K < C.relicKinds.size(); ++K)
				{
					R.kind = C.relicKinds[K].id == RJ.GetString("kind") ? static_cast<int>(K) : R.kind;
				}
				R.rarity = std::max(1, std::min(5, RJ.GetInt("rarity", 1)));
				R.durability = std::max(0, RJ.GetInt("durability", 0));
				if (R.kind >= 0)
				{
					RelicList.push_back(R);
				}
			}
		}
	}
	// Drop references to relics or titans that are gone.
	for (TitanState& T : TitanList)
	{
		T.relics.erase(std::remove_if(T.relics.begin(), T.relics.end(), [this](int32_t Id) { return !FindRelic(Id); }), T.relics.end());
	}
	for (Land& L : LandList)
	{
		if (L.titanId && !FindTitan(L.titanId))
		{
			L.titanId = 0;
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
		W.I32(R.durability);
	}
	return W.Hash();
}
} // namespace kingdom
} // namespace dot
