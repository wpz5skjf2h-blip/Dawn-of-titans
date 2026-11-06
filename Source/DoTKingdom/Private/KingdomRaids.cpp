// Source/DoTKingdom/Private/KingdomRaids.cpp
// Raids on the kingdom. The original (wiki, guides): the city and captured lands have default defenses that the
// player reinforces with titans and troops, a lost defense gives a shield, reserves keep part of the gold and food
// from raiders, and since 1.8 a raid on the Garrison takes 5% of the defender's VP. There the raiders are other
// players; here they are AI raiders, and they also come while the game is closed: Advance plays every raid that came
// in between, in time order, until the player has been away for kingdom.json raids.awayHours.
// A raid is decided by the power of the two sides (the battle simulation's own measure of squads and titans) with
// odds fitted to simulated battles of the same armies, so a long absence costs no battle simulations.
#include "DoTKingdom/Kingdom.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Hash.h"

#include <algorithm>

namespace dot
{
namespace kingdom
{
namespace
{
uint64_t RaidMix(uint64_t Seed, uint64_t Counter)
{
	uint64_t Data[2] = {Seed ^ 0x7261696473ULL, Counter};
	return XXHash64(Data, sizeof(Data), 0x6b696e67646f6dULL);
}

/** Share of the loot by the raiders' stars, as for the player's own raids. */
int64_t StarsPct(int Stars)
{
	return Stars >= 3 ? 100 : (Stars == 2 ? 75 : (Stars == 1 ? 50 : 0));
}

SquadSetup MakeSquad(const std::string& Troop, int Tier)
{
	SquadSetup Q;
	Q.troopId = Troop;
	Q.tier = Tier;
	return Q;
}

// The Marshal: the enemy's titan counts with its Power as the battle has it; a repeated squad of the plan is worth
// DecayPct % of the one before (a counter army still mixes troops); a class of squads is a threat with a quarter of the
// squads' Power; a troop hits it hard from GoodPct (the matrix has 1.3 for "strong against") and a class hits a squad
// hard from BadPct.
constexpr int64_t DecayPct = 90;
constexpr int64_t ThreatPct = 25;
constexpr int64_t GoodPct = 130;
constexpr int64_t BadPct = 150;
} // namespace

void Kingdom::ResetRaids(int64_t Now)
{
	NextRaid = Now + C.raids.firstSec;
	VisitAt = Now;
	RaidCounter = 0;
	CityShield = 0;
	RaidsRead = Now;
	GarrisonList.clear();
	GarrisonTitanId = 0;
	RaidLog.clear();
}

void Kingdom::Visit()
{
	// Raiders that stopped coming while the player was away start again from now.
	if (NextRaid <= Time)
	{
		const RaidRules& R = C.raids;
		const uint64_t H = RaidMix(Seed, RaidCounter++);
		NextRaid = Time + R.everyMinSec + static_cast<int64_t>(H % static_cast<uint64_t>(R.everyMaxSec - R.everyMinSec + 1));
	}
	// A week or more since the last visit earns the comeback gift (a longer absence replaces one not claimed yet).
	const int64_t Away = Time - VisitAt;
	if (C.comeback.days > 0 && VisitAt > 0 && Away >= static_cast<int64_t>(C.comeback.days) * 86400 && Away > ComebackSec && CastleLevel() >= C.comeback.castle)
	{
		ComebackSec = Away;
		ComebackAt = Time;
	}
	VisitAt = Time;
	NoteLogin();
}

int64_t Kingdom::NextRaidAt() const
{
	return BattleContent && NextRaid <= VisitAt + C.raids.awaySec ? NextRaid : 0;
}

int Kingdom::DefenseSlots() const
{
	const LevelDef* L = C.garrison >= 0 ? BuildingLevel(C.garrison) : nullptr;
	return L && L->defenseSlots > 0 ? static_cast<int>(L->defenseSlots) : C.raids.defaultSlots;
}

int Kingdom::GuardTier() const
{
	const LevelDef* L = C.garrison >= 0 ? BuildingLevel(C.garrison) : nullptr;
	return L && L->guardTier > 0 ? static_cast<int>(L->guardTier) : C.raids.defaultGuardTier;
}

int64_t Kingdom::DefenseBonusPct() const
{
	const LevelDef* L = C.garrison >= 0 ? BuildingLevel(C.garrison) : nullptr;
	return L ? L->defenseBonusPct : 0;
}

int Kingdom::RaidLevelLow() const
{
	return LandBandLow(CastleLevel());
}

int Kingdom::RaidLevelHigh() const
{
	return LandBandTop(CastleLevel());
}

std::vector<int> Kingdom::RaiderTitans(int Level) const
{
	// Titans whose rarity fits the level: R1 to level 3, R1-R2 to 6, R2-R3 to 8, then R3-R4.
	std::vector<int> Pool;
	if (RaiderTitanLevel(Level) <= 0)
	{
		return Pool;
	}
	const int Lo = Level <= 6 ? 1 : (Level <= 8 ? 2 : 3);
	const int Hi = Level <= 3 ? 1 : Lo + 1;
	for (size_t I = 0; I < C.titans.size(); ++I)
	{
		if (C.titans[I].stars >= Lo && C.titans[I].stars <= Hi)
		{
			Pool.push_back(static_cast<int>(I));
		}
	}
	return Pool;
}

int Kingdom::RaiderTitanLevel(int Level) const
{
	const std::vector<int64_t>& Levels = C.raids.titanLevels;
	if (!Levels.empty())
	{
		return static_cast<int>(Levels[static_cast<size_t>(std::max(1, std::min(Level, static_cast<int>(Levels.size()))) - 1)]);
	}
	return C.battles[static_cast<size_t>(std::max(1, std::min(Level, static_cast<int>(C.battles.size()))) - 1)].titanLevel;
}

void Kingdom::RaidArmies(int32_t LandId, int Level, uint64_t Pick, int64_t At, SideSetup& Raiders, SideSetup& Defense, int* OutSquads, int* OutGuards,
	int32_t* OutTitan) const
{
	const RaidRules& R = C.raids;
	Raiders = SideSetup();
	Defense = SideSetup();
	if (C.battles.empty())
	{
		return;
	}
	const BattleRowDef& Row = C.battles[static_cast<size_t>(std::max(1, std::min(Level, static_cast<int>(C.battles.size()))) - 1)];
	const Land* L = LandId ? FindLand(LandId) : nullptr;
	// Raiders: the level's battle army (on the city as many squads as raiders of that level bring) and a titan.
	Raiders.aiProfile = Row.ai;
	int Count = static_cast<int>(Row.squads.size());
	if (!L && Level >= 1 && Level <= static_cast<int>(R.citySquads.size()))
	{
		Count = static_cast<int>(std::max<int64_t>(1, R.citySquads[static_cast<size_t>(Level - 1)]));
	}
	for (int I = 0; I < Count && !Row.squads.empty(); ++I)
	{
		Raiders.squads.push_back(MakeSquad(Row.squads[static_cast<size_t>(I) % Row.squads.size()], Row.tier));
	}
	const std::vector<int> Pool = RaiderTitans(Level);
	if (!Pool.empty())
	{
		const TitanType& T = C.titans[static_cast<size_t>(Pool[static_cast<size_t>(Pick % Pool.size())])];
		Raiders.titan.present = true;
		Raiders.titan.titanId = T.id;
		Raiders.titan.stars = T.stars;
		Raiders.titan.level = std::min(RaiderTitanLevel(Level), T.MaxLevel());
	}
	// Defense: a land's own defenders (the land type changes some, as on the world map) or the Garrison's slots,
	// filled with the player's squads at home and then the Garrison's guards; plus the titan on guard if it is rested.
	Defense.aiProfile = R.defenderAi;
	int Mine = 0;
	int Guards = 0;
	int32_t Guard = 0;
	if (L)
	{
		// The land's defenders stand at its own level, so an upgraded land defends better against the same raiders.
		const BattleRowDef& Own = C.battles[static_cast<size_t>(std::max(1, std::min(L->level, static_cast<int>(C.battles.size()))) - 1)];
		const LandTypeDef& Type = C.landTypes[static_cast<size_t>(L->type)];
		for (const std::string& Id : Own.squads)
		{
			std::string Troop = Id;
			for (const auto& Swap : Type.swap)
			{
				if (Swap.first == Id)
				{
					Troop = Swap.second;
				}
			}
			Defense.squads.push_back(MakeSquad(Troop, Own.tier));
		}
		Guard = L->titanId;
	}
	else
	{
		const int Slots = DefenseSlots();
		std::vector<int64_t> Home = Army;
		for (const int T : PendingArmy)
		{
			--Home[static_cast<size_t>(T)];
		}
		for (const int T : GarrisonList)
		{
			if (Mine < Slots && T >= 0 && T < static_cast<int>(Home.size()) && Home[static_cast<size_t>(T)] > 0)
			{
				--Home[static_cast<size_t>(T)];
				const TroopType& Troop = C.troops[static_cast<size_t>(T)];
				Defense.squads.push_back(MakeSquad(Troop.id, Troop.levels[static_cast<size_t>(TroopLevel(T) - 1)].tier));
				// The troop's War Banner goes with it.
				Defense.squads.back().hpPct = BannerPct(T);
				Defense.squads.back().atkPct = BannerPct(T);
				++Mine;
			}
		}
		for (; Mine + Guards < Slots && !R.guards.empty(); ++Guards)
		{
			Defense.squads.push_back(MakeSquad(R.guards[static_cast<size_t>(Guards) % R.guards.size()], GuardTier()));
		}
		Guard = GarrisonTitanId;
	}
	// A titan defends when it is rested and not away in the player's own attack.
	const TitanState* T = Guard && Guard != PendingTitanId ? FindTitan(Guard) : nullptr;
	if (T && T->recoverUntil <= At)
	{
		TitanBattleSetup(*T, Defense.titan);
	}
	if (OutSquads)
	{
		*OutSquads = Mine;
	}
	if (OutGuards)
	{
		*OutGuards = Guards;
	}
	if (OutTitan)
	{
		*OutTitan = Defense.titan.present ? T->id : 0;
	}
}

void Kingdom::TitanBattleSetup(const TitanState& T, TitanSetup& Out) const
{
	const TitanType& Type = C.titans[static_cast<size_t>(T.def)];
	Out.present = true;
	Out.titanId = Type.id;
	Out.stars = Type.stars;
	Out.level = T.level;
	Out.ascended = T.ascended;
	// Relic and skill bonuses are in tenths of a percent, the sim takes whole percents (as the battle page does).
	Out.atkPct = static_cast<int>(TitanBonus(T.id, RelicStat::Atk) / 10);
	Out.hpPct = static_cast<int>(TitanBonus(T.id, RelicStat::Hp) / 10);
	Out.armorPct = static_cast<int>(TitanBonus(T.id, RelicStat::Armor) / 10);
	Out.critPct = static_cast<int>(TitanBonus(T.id, RelicStat::Crit) / 10);
	Out.piercePct = static_cast<int>(TitanBonus(T.id, RelicStat::Pierce) / 10);
}

int64_t Kingdom::ArmyPower(const SideSetup& Side) const
{
	if (!BattleContent)
	{
		return 0;
	}
	int64_t Power = 0;
	for (const SquadSetup& Q : Side.squads)
	{
		Power += SquadPower(*BattleContent, Q);
	}
	Power += TitanPower(*BattleContent, Side.titan) * C.raids.titanPct / 100;
	for (const auto& Ai : C.raids.aiPct)
	{
		if (Ai.first == Side.aiProfile)
		{
			return Power * Ai.second / 100;
		}
	}
	return Power;
}

int64_t Kingdom::OddsPower(int64_t Squads, int64_t Titans, const std::string& Ai) const
{
	const int64_t Power = Squads + Titans * C.raids.titanPct / 100;
	for (const auto& A : C.raids.aiPct)
	{
		if (!Ai.empty() && A.first == Ai)
		{
			return Power * A.second / 100;
		}
	}
	return Power;
}

int64_t Kingdom::BattleWinPermille(int64_t AttackSquads, int64_t AttackTitans, int64_t DefenseSquads, int64_t DefenseTitans, const std::string& DefenseAi) const
{
	return RaidWinPermille(OddsPower(AttackSquads, AttackTitans, std::string()), OddsPower(DefenseSquads, DefenseTitans, DefenseAi));
}

MarshalAdvice Kingdom::Marshal(const SideSetup& Enemy, const std::vector<int>& Taken, const std::vector<bool>& Allowed) const
{
	MarshalAdvice Out;
	const size_t N = C.troops.size();
	Out.plan.assign(N, 0);
	if (!BattleContent)
	{
		return Out;
	}
	const SimContent& B = *BattleContent;
	const auto Pct = [&B](int Row, int Cls) { return (B.counters.Get(Row, static_cast<DefenderClass>(Cls)) * 100).RoundToInt(); };
	struct Foe
	{
		int cls = 0;
		int row = -1;
		int64_t w = 0;
	};
	std::vector<Foe> Foes;
	for (const SquadSetup& Q : Enemy.squads)
	{
		const int I = B.FindTroop(Q.troopId);
		if (I >= 0)
		{
			const TroopDef& D = B.troops[static_cast<size_t>(I)];
			Foes.push_back({static_cast<int>(D.cls), D.counterRow, SquadPower(B, Q)});
		}
	}
	if (Enemy.titan.present)
	{
		Foes.push_back({static_cast<int>(DefenderClass::Titan), B.titanCounterRow, TitanPower(B, Enemy.titan)});
	}
	int64_t W = 0;
	int64_t ClassW[kDefenderClassCount] = {};
	for (const Foe& F : Foes)
	{
		W += F.w;
		ClassW[F.cls] += F.w;
	}
	if (W <= 0)
	{
		return Out;
	}
	// Each troop at its level: its Power times the multiplier it deals over the one it takes, both by the enemy's Power.
	struct Mine
	{
		int cls = 0;
		int row = -1;
		int64_t power = 0;
		int64_t value = 0;
		bool open = false; // may be trained now and fights by the battle's rule
		bool ok = false; // in the battle content and by the rule
	};
	std::vector<Mine> M(N);
	for (size_t T = 0; T < N; ++T)
	{
		const TroopType& Tt = C.troops[T];
		const int I = B.FindTroop(Tt.id);
		if (I < 0 || Tt.levels.empty() || (T < Allowed.size() && !Allowed[T]))
		{
			continue;
		}
		const TroopDef& D = B.troops[static_cast<size_t>(I)];
		const int Lv = std::min(static_cast<int>(Tt.levels.size()), std::max(1, TroopLevel(static_cast<int>(T))));
		Mine& Me = M[T];
		Me.cls = static_cast<int>(D.cls);
		Me.row = D.counterRow;
		Me.power = SquadPower(B, MakeSquad(Tt.id, Tt.levels[static_cast<size_t>(Lv - 1)].tier));
		int64_t Deal = 0;
		int64_t Take = 0;
		for (const Foe& F : Foes)
		{
			Deal += F.w * Pct(Me.row, F.cls);
			Take += F.w * Pct(F.row, Me.cls);
		}
		Me.value = Take > 0 ? Me.power * (Deal / W) / std::max<int64_t>(1, Take / W) : Me.power;
		Me.ok = true;
		Me.open = CastleLevel() >= Tt.unlockCastle && (Tt.council < 0 || CouncilLevel(static_cast<int>(T)) >= Tt.councilLevel);
	}
	// The plan: the best next squad from the camp, slot by slot (the best mix for a value that drops with each repeat).
	const auto Marginal = [&M](size_t T, int Have) {
		int64_t V = M[T].value;
		for (int K = 0; K < Have; ++K)
		{
			V = V * DecayPct / 100;
		}
		return V;
	};
	for (int Slot = 0; Slot < BattleSlots(); ++Slot)
	{
		int Best = -1;
		int64_t BestV = 0;
		for (size_t T = 0; T < N; ++T)
		{
			if (M[T].ok && Out.plan[T] < ArmyCount(static_cast<int>(T)))
			{
				const int64_t V = Marginal(T, Out.plan[T]);
				if (Best < 0 || V > BestV)
				{
					Best = static_cast<int>(T);
					BestV = V;
				}
			}
		}
		if (Best < 0)
		{
			break;
		}
		++Out.plan[static_cast<size_t>(Best)];
		Out.planScore += BestV;
	}
	for (size_t T = 0; T < N && T < Taken.size(); ++T)
	{
		for (int K = 0; K < Taken[T]; ++K)
		{
			Out.takenScore += M[T].ok ? Marginal(T, K) : 0;
		}
	}
	// Tips: the threats, strongest first. A titan always is one; a class of squads with a quarter of the squads' Power is
	// (the titan's own Power would hide every squad behind it).
	const int TitanCls = static_cast<int>(DefenderClass::Titan);
	const int64_t SquadW = W - ClassW[TitanCls];
	const auto IsThreat = [&](int Cls) { return Cls == TitanCls ? ClassW[Cls] > 0 : SquadW > 0 && ClassW[Cls] * 100 >= SquadW * ThreatPct; };
	std::vector<int> Threats;
	for (int Cls = 0; Cls < kDefenderClassCount; ++Cls)
	{
		if (IsThreat(Cls))
		{
			Threats.push_back(Cls);
		}
	}
	std::stable_sort(Threats.begin(), Threats.end(), [&ClassW](int A, int Bc) { return ClassW[A] > ClassW[Bc]; });
	const auto TakenOf = [&Taken](size_t T) { return T < Taken.size() ? std::max(0, Taken[T]) : 0; };
	// A counter is worth naming only when it would raise the army's score: a free slot, or more than the weakest squad
	// taken is worth (so the tips never argue with the plan).
	int TakenN = 0;
	int64_t Weakest = -1;
	for (size_t T = 0; T < N; ++T)
	{
		if (TakenOf(T) > 0)
		{
			TakenN += TakenOf(T);
			const int64_t V = M[T].ok ? Marginal(T, TakenOf(T) - 1) : 0;
			Weakest = Weakest < 0 ? V : std::min(Weakest, V);
		}
	}
	const bool FreeSlot = TakenN < BattleSlots();
	const auto Worth = [&](size_t T) { return FreeSlot || Marginal(T, TakenOf(T)) > Weakest; };
	std::vector<MarshalTip> Counter;
	for (const int Cls : Threats)
	{
		// The hardest hitter in the camp with a squad to spare, else the hardest one that can be trained; nothing if a squad
		// taken hits nearly as hard.
		int InCamp = -1;
		int Trainable = -1;
		int64_t Covered = 0;
		for (size_t T = 0; T < N; ++T)
		{
			if (!M[T].ok)
			{
				continue;
			}
			const int64_t P = Pct(M[T].row, Cls);
			if (TakenOf(T) > 0)
			{
				Covered = std::max(Covered, P);
			}
			if (P < GoodPct || !Worth(T))
			{
				continue;
			}
			const auto Better = [&](int Cur) {
				if (Cur < 0)
				{
					return true;
				}
				const int64_t Q = Pct(M[static_cast<size_t>(Cur)].row, Cls);
				return P > Q || (P == Q && M[T].value > M[static_cast<size_t>(Cur)].value);
			};
			if (ArmyCount(static_cast<int>(T)) > TakenOf(T) && Better(InCamp))
			{
				InCamp = static_cast<int>(T);
			}
			if (M[T].open && ArmyCount(static_cast<int>(T)) <= 0 && Better(Trainable))
			{
				Trainable = static_cast<int>(T);
			}
		}
		const int Pick = InCamp >= 0 ? InCamp : Trainable;
		if (Pick < 0)
		{
			continue;
		}
		const int64_t P = Pct(M[static_cast<size_t>(Pick)].row, Cls);
		if (Covered >= GoodPct && Covered + 25 >= P)
		{
			continue;
		}
		Counter.push_back({InCamp >= 0 ? MarshalTipKind::Bring : MarshalTipKind::Train, Pick, Cls, static_cast<int>(P)});
	}
	// The squad taken that a threat hits hardest, of those the plan would take fewer of.
	MarshalTip Weak;
	Weak.troop = -1;
	for (size_t T = 0; T < N; ++T)
	{
		if (!M[T].ok || TakenOf(T) <= Out.plan[T])
		{
			continue;
		}
		for (const Foe& F : Foes)
		{
			const int64_t P = Pct(F.row, M[T].cls);
			if (IsThreat(F.cls) && P >= BadPct && (Weak.troop < 0 || P > Weak.pct))
			{
				Weak = {MarshalTipKind::Exposed, static_cast<int>(T), F.cls, static_cast<int>(P)};
			}
		}
	}
	for (size_t I = 0; I < Counter.size(); ++I)
	{
		Out.tips.push_back(Counter[I]);
		if (I == 0 && Weak.troop >= 0)
		{
			Out.tips.push_back(Weak);
		}
	}
	if (Counter.empty() && Weak.troop >= 0)
	{
		Out.tips.push_back(Weak);
	}
	if (Out.tips.size() > 3)
	{
		Out.tips.resize(3);
	}
	return Out;
}

int64_t Kingdom::DefensePower(const SideSetup& Defense, int32_t LandId) const
{
	return ArmyPower(Defense) * (100 + (LandId ? 0 : DefenseBonusPct())) / 100;
}

int64_t Kingdom::RaidWinPermille(int64_t Raid, int64_t Defense) const
{
	if (Raid <= 0 || Defense <= 0)
	{
		return Raid > 0 ? 1000 : 0;
	}
	// 1 / (1 + y^k) with y = c / x = c * defense / raiders, clamped to 1/16..16 (the odds are all but certain there).
	const RaidRules& R = C.raids;
	const int64_t Scaled = std::max<int64_t>(256, std::min<int64_t>(65536, R.oddsC * Defense * 4096 / (100 * Raid)));
	const Fix64 Root = fx::Sqrt(fx::Sqrt(Fix64::FromRatio(Scaled, 4096)));
	Fix64 Pow = Fix64::One();
	for (int I = 0; I < R.oddsKQuarters; ++I)
	{
		Pow = Pow * Root;
	}
	return (fx::Int(1000) / (Fix64::One() + Pow)).RoundToInt();
}

void Kingdom::RunRaid(int64_t At)
{
	const RaidRules& R = C.raids;
	const uint64_t H = RaidMix(Seed, RaidCounter++);
	const uint64_t H2 = RaidMix(H, RaidCounter);
	NextRaid = At + R.everyMinSec + static_cast<int64_t>(H % static_cast<uint64_t>(R.everyMaxSec - R.everyMinSec + 1));
	if (CastleLevel() < R.castle || C.battles.empty())
	{
		return;
	}
	// Targets: the city and the captured lands without a shield.
	const bool bCityOpen = CityShield <= At;
	std::vector<size_t> Open;
	for (size_t I = 0; I < LandList.size(); ++I)
	{
		if (LandList[I].shieldUntil <= At)
		{
			Open.push_back(I);
		}
	}
	if (!bCityOpen && Open.empty())
	{
		return;
	}
	const bool bCity = bCityOpen && (Open.empty() || static_cast<int64_t>((H >> 24) % 100) < R.cityPct);
	Land* L = bCity ? nullptr : &LandList[Open[static_cast<size_t>((H >> 32) % Open.size())]];
	RaidReport Rep;
	Rep.at = At;
	// Raiders come at a level of the Castle's band (as the world lands it opens).
	const int Lo = RaidLevelLow();
	Rep.level = Lo + static_cast<int>((H >> 40) % static_cast<uint64_t>(RaidLevelHigh() - Lo + 1));
	if (L)
	{
		SettleLand(*L, At);
		Rep.landId = L->id;
		Rep.landType = L->type;
	}
	SideSetup Raiders;
	SideSetup Defense;
	RaidArmies(Rep.landId, Rep.level, H2, At, Raiders, Defense, &Rep.squads, nullptr, &Rep.titanId);
	Rep.raidPower = ArmyPower(Raiders);
	Rep.defensePower = DefensePower(Defense, Rep.landId);
	if (static_cast<int64_t>((H2 >> 20) % 1000) < RaidWinPermille(Rep.raidPower, Rep.defensePower))
	{
		const int64_t Ratio = Rep.raidPower * 100 / std::max<int64_t>(1, Rep.defensePower);
		// Without a titan in the defense its star comes with the first one, as in battle.
		Rep.stars = Ratio >= R.threeStarRatio ? 3 : (Ratio >= R.twoStarRatio || !Defense.titan.present ? 2 : 1);
		const int64_t Pct = StarsPct(Rep.stars);
		if (L)
		{
			const int64_t Take = L->stored * R.landStealPct * Pct / 10000;
			L->stored -= Take;
			Rep.lost[static_cast<size_t>(ResIndex(C.landTypes[static_cast<size_t>(L->type)].resource))] = Take;
			L->shieldUntil = At + R.landShieldSec;
			Rep.shieldUntil = L->shieldUntil;
		}
		else
		{
			// Gold and food above what the reserves protect, and a share of the VP (wiki 1.8: 5%).
			for (const Res X : {Res::Gold, Res::Food})
			{
				const int64_t Take = std::max<int64_t>(0, Amount(X) - Protected(X)) * R.stealPct * Pct / 10000;
				Res_[static_cast<size_t>(ResIndex(X))] -= Take;
				Rep.lost[static_cast<size_t>(ResIndex(X))] = Take;
			}
			const int64_t Vp = Amount(Res::Vp) * R.vpPct * Pct / 10000;
			Res_[static_cast<size_t>(ResIndex(Res::Vp))] -= Vp;
			Rep.lost[static_cast<size_t>(ResIndex(Res::Vp))] = Vp;
			CityShield = At + R.cityShieldSec;
			Rep.shieldUntil = CityShield;
		}
	}
	else
	{
		// A held defense is a victory too (wiki: VP for almost all victories).
		Rep.vp = VictoryVp(Rep.level) * R.defenseVpPct / 100;
		AddResource(Res::Vp, Rep.vp);
	}
	RaidLog.push_back(Rep);
	if (static_cast<int>(RaidLog.size()) > R.log)
	{
		RaidLog.erase(RaidLog.begin(), RaidLog.end() - R.log);
	}
}

RaidForecast Kingdom::Forecast(int32_t LandId) const
{
	RaidForecast F;
	const Land* L = LandId ? FindLand(LandId) : nullptr;
	if ((LandId && !L) || !BattleContent || C.battles.empty())
	{
		return F;
	}
	F.active = CastleLevel() >= C.raids.castle;
	F.levelLow = RaidLevelLow();
	F.levelHigh = RaidLevelHigh();
	// Every level and titan the raiders can come with, equally likely.
	int64_t RaidSum = 0;
	int64_t WinSum = 0;
	int64_t N = 0;
	for (int Level = F.levelLow; Level <= F.levelHigh; ++Level)
	{
		const size_t Picks = std::max<size_t>(1, RaiderTitans(Level).size());
		for (size_t P = 0; P < Picks; ++P)
		{
			SideSetup Raiders;
			SideSetup Defense;
			RaidArmies(LandId, Level, P, Time, Raiders, Defense, &F.squads, &F.guards, &F.titanId);
			const int64_t Raid = ArmyPower(Raiders);
			F.defense = DefensePower(Defense, LandId);
			RaidSum += Raid;
			WinSum += RaidWinPermille(Raid, F.defense);
			++N;
		}
	}
	F.raiders = RaidSum / N;
	F.holdPct = static_cast<int>((1000 * N - WinSum) / N / 10);
	return F;
}

Error Kingdom::SetGarrison(const std::vector<int>& Squads, int32_t TitanId)
{
	if (TitanId && !FindTitan(TitanId))
	{
		return Error::BadId;
	}
	if (static_cast<int>(Squads.size()) > DefenseSlots())
	{
		return Error::TooManySquads;
	}
	std::vector<int64_t> Count(C.troops.size(), 0);
	for (const int T : Squads)
	{
		if (T < 0 || T >= static_cast<int>(C.troops.size()))
		{
			return Error::BadId;
		}
		if (++Count[static_cast<size_t>(T)] > Army[static_cast<size_t>(T)])
		{
			return Error::NoTroops;
		}
	}
	GarrisonList = Squads;
	if (TitanId != GarrisonTitanId)
	{
		// A titan guards one place at a time: it leaves the land it was on.
		SettleLands();
		for (Land& L : LandList)
		{
			if (TitanId && L.titanId == TitanId)
			{
				L.titanId = 0;
			}
		}
		GarrisonTitanId = TitanId;
	}
	return Error::None;
}

Error Kingdom::ReadRaids()
{
	RaidsRead = Time;
	return Error::None;
}

void Kingdom::ClearGuard(int32_t TitanId)
{
	if (TitanId && GarrisonTitanId == TitanId)
	{
		GarrisonTitanId = 0;
	}
}
// --- Kingdom Power (master prompt 7.7) ---

int64_t Kingdom::TitanPowerOf(int32_t TitanId) const
{
	const TitanState* T = FindTitan(TitanId);
	if (!T || !BattleContent)
	{
		return 0;
	}
	TitanSetup Setup;
	TitanBattleSetup(*T, Setup);
	// The simulation's Power is sqrt(EHP × DPS × AoE) with EHP and DPS in hundredths, so 100 times the prompt's.
	return TitanPower(*BattleContent, Setup) / 100;
}

KingdomPowerInfo Kingdom::KingdomPower() const
{
	const PowerRules& P = C.power;
	KingdomPowerInfo Out;
	for (const Building& B : BuildingList)
	{
		if (B.level > 0)
		{
			// building × L^1.5 = sqrt(building² × L³), rounded down.
			const uint64_t L = static_cast<uint64_t>(B.level);
			const int64_t Each = static_cast<int64_t>(ISqrt64(static_cast<uint64_t>(P.building * P.building) * L * L * L));
			Out.buildings += B.def == C.castle ? Each * P.castleMul : Each;
		}
	}
	if (BattleContent)
	{
		for (const TitanState& T : TitanList)
		{
			Out.top.emplace_back(T.id, TitanPowerOf(T.id));
		}
		std::sort(Out.top.begin(), Out.top.end(), [](const std::pair<int32_t, int64_t>& A, const std::pair<int32_t, int64_t>& B) {
			return A.second != B.second ? A.second > B.second : A.first < B.first;
		});
		if (Out.top.size() > static_cast<size_t>(P.titans))
		{
			Out.top.resize(static_cast<size_t>(P.titans));
		}
		for (const auto& T : Out.top)
		{
			Out.titans += T.second;
		}
	}
	for (const int L : TroopLevels)
	{
		Out.research += (L - 1) * P.research;
	}
	for (const int L : SpellLevelList)
	{
		Out.research += (L - 1) * P.research;
	}
	for (const int L : BannerList)
	{
		Out.research += L * P.research; // War Banners
	}
	Out.codex = C.codex.on ? CodexEntries() * C.codex.power : 0;
	Out.total = Out.buildings + Out.titans + Out.research + Out.codex;
	return Out;
}

} // namespace kingdom
} // namespace dot
