/*
** Copyright (c) 2015, Braden "Blzut3" Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * The names of its contributors may be used to endorse or promote
**       products derived from this software without specific prior written
**       permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
** INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "scanner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

// Check the current string against a given list of potential keywords. Returns
// the index or -1.
int CheckKeyword(const Scanner &sc, std::initializer_list<const char*> keywords)
{
	int i = 0;
	for(const char *word : keywords)
	{
		if(stricmp(word, sc->str.c_str()) == 0)
			return i;
		++i;
	}
	return -1;
}

// Same as above except throws an error if there's no match.
int RequireKeyword(const Scanner &sc, std::initializer_list<const char*> keywords)
{
	int ret;
	if((ret = CheckKeyword(sc, keywords)) >= 0)
		return ret;
	sc.ScriptMessage(Scanner::ERROR, "Invalid keyword at this point.");
	return -1;
}

// Helper function for when we need to parse a signed integer.
int GetNegativeInteger(Scanner &sc)
{
	bool neg = sc.CheckToken('-');
	sc.MustGetToken(TK_IntConst);
	return neg ? -sc->number : sc->number;
}

// Converts a string to upper case.
static inline void ToUpper(std::string &str)
{
	std::transform(str.begin(), str.end(), str.begin(), toupper);
}

// ----------------------------------------------------------------------------

// Flags!
enum
{
	MF_SPECIAL = 1,
	MF_SOLID = 2,
	MF_SHOOTABLE = 4,
	MF_NOSECTOR = 8,
	MF_NOBLOCKMAP = 0x10,
	MF_AMBUSH = 0x20,
	MF_JUSTHIT = 0x40,
	MF_JUSTATTACKED = 0x80,
	MF_SPAWNCEILING = 0x100,
	MF_NOGRAVITY = 0x200,
	MF_DROPOFF = 0x400,
	MF_PICKUP = 0x800,
	MF_NOCLIP = 0x1000,
	MF_SLIDE = 0x2000,
	MF_FLOAT = 0x4000,
	MF_TELEPORT = 0x8000,
	MF_MISSILE = 0x10000,
	MF_DROPPED = 0x20000,
	MF_SHADOW = 0x40000,
	MF_NOBLOOD = 0x80000,
	MF_CORPSE = 0x100000,
	MF_INFLOAT = 0x200000,
	MF_COUNTKILL = 0x400000,
	MF_COUNTITEM = 0x800000,
	MF_SKULLFLY = 0x1000000,
	MF_NOTDMATCH = 0x2000000,
	MF_TRANSLATION1 = 0x4000000,
	MF_TRANSLATION2 = 0x8000000
};

struct
{
	const char* name;
	uint32_t flag;
} Flags[] = {
	{"SPECIAL", MF_SPECIAL},
	{"SOLID", MF_SOLID},
	{"SHOOTABLE", MF_SHOOTABLE},
	{"NOSECTOR", MF_NOSECTOR},
	{"NOBLOCKMAP", MF_NOBLOCKMAP},
	{"AMBUSH", MF_AMBUSH},
	{"JUSTHIT", MF_JUSTHIT},
	{"JUSTATTACKED", MF_JUSTATTACKED},
	{"SPAWNCEILING", MF_SPAWNCEILING},
	{"NOGRAVITY", MF_NOGRAVITY},
	{"DROPOFF", MF_DROPOFF},
	{"PICKUP", MF_PICKUP},
	{"NOCLIP", MF_NOCLIP},
	{"SLIDE", MF_SLIDE},
	{"FLOAT", MF_FLOAT},
	{"TELEPORT", MF_TELEPORT},
	{"MISSILE", MF_MISSILE},
	{"DROPPED", MF_DROPPED},
	{"SHADOW", MF_SHADOW},
	{"NOBLOOD", MF_NOBLOOD},
	{"CORPSE", MF_CORPSE},
	{"INFLOAT", MF_INFLOAT},
	{"COUNTKILL", MF_COUNTKILL},
	{"COUNTITEM", MF_COUNTITEM},
	{"SKULLFLY", MF_SKULLFLY},
	{"NOTDMATCH", MF_NOTDMATCH},
	{"TRANSLATION1", MF_TRANSLATION1},
	{"TRANSLATION2", MF_TRANSLATION2},
	{NULL, 0}
};

// Lookup a sound by logical name and return the index into the sound table.
int LookupSound(const std::string &sound)
{
	return 0;
}

// ----------------------------------------------------------------------------

// Structure for assigning action function to actor classes.
// As a general note, when we have a pointer to a table we just use an integer
// so we don't need to worry about invalidation.
struct Action
{
	std::string function;
	int actor;
};

// Prepresents a label in DECORATE, points to the internal state table and then
// after installation to the global state table.
struct Label
{
	Label() : state(0), tablepos(-1) {}

	std::string label;
	std::string codelabel; // Used to generate a name for C
	int state;

	int tablepos; // Position in StateTable
};

enum ESequence
{
	SEQ_Next,
	SEQ_Goto,
	SEQ_Stop,
	SEQ_Wait,
	SEQ_Loop
};
// Represents a parsed state name. [Class::] label [+ #]
struct StateLink
{
	StateLink() : sequence(SEQ_Next), jumpOffset(0) {}

	ESequence sequence;
	std::string jumpState;
	std::string jumpClass;
	int jumpOffset;
};

// DECORATE state which may expand to multiple State objects during
// the installation process.
struct DState
{
	DState() : action(-1), duration(0), xoffset(0), yoffset(0), bright(false) {}

	std::string sprite;
	std::string frames;
	StateLink next;

	int action;
	int duration;
	int xoffset;
	int yoffset;
	bool bright;
};

// Our mobj information.
struct Actor
{
	Actor() : classNum(0), parent(0), doomednum(-1), native(false) {}

	std::string name;
	int classNum;
	int parent;
	int doomednum;
	bool native;

	// Should be able to be memset/memcpy'd
	struct
	{
		int spawnhealth;
		int seesound;
		int reactiontime;
		int attacksound;
		int painchance;
		int painsound;
		int deathsound;
		int speed;
		int radius;
		int height;
		int mass;
		int damage;
		int activesound;
		uint32_t flags;
	} props;

	// Local tables
	std::vector<DState> states;
	std::vector<Label> labels;
};

std::vector<std::string> SpriteNames;
std::vector<Action> ActionFunctions;
std::vector<Actor> ActorClasses;

// ----------------------------------------------------------------------------

// Generates new C frame labels based on the nearest label given. If no C label
// is given then it will use the DECORATE label in combination with the actor
// name.
struct FrameLabel
{
	std::string Assign()
	{
		auto tmp = label;
		label.erase(label.begin()+stemend, label.end());

		++frame;
		char num[10];
		snprintf(num, 10, "%d", frame);
		label += num;
		return tmp;
	}

	void Reset(const Actor &actor, const Label &lbl)
	{
		if(lbl.codelabel.empty())
		{
			label = actor.name + "_" + lbl.label;
			ToUpper(label);
		}
		else
			label = lbl.codelabel;

		stemend = label.length();
		while(isdigit(label[stemend-1]))
			--stemend;

		if(stemend != label.length())
			frame = atoi(label.c_str()+stemend);
		else
			frame = 1;
	}

private:
	std::string label;
	int stemend;
	int frame;
};

// Structure for installed state.
struct State
{
	std::string label;
	std::string sprite;
	unsigned frame;
	int duration;
	int action;
	int next;
	int xoffset;
	int yoffset;

	bool bright;
};

std::vector<State> StateTable;

// Structure used for deferring goto resolution until all states are installed.
struct Resolve
{
	unsigned int statenum;
	StateLink link;
};

// Resolve a sate name searching the parent if the state can't be found locally.
const State *FindState(const Actor &actor, const StateLink &link)
{
	const Actor *check = &actor;

	if(!link.jumpClass.empty())
	{
		if(stricmp(link.jumpClass.c_str(), "Super") == 0)
			check = &ActorClasses[actor.parent];
		else
		{
			fprintf(stderr, "Only Super:: is supported with goto.\n");
			exit(0);
		}
	}

	do
	{
		auto state = std::find_if(check->labels.begin(), check->labels.end(),
								  [link](const Label &lbl) { return stricmp(lbl.label.c_str(), link.jumpState.c_str()) == 0; });
		if(state == check->labels.end())
		{
			if(check->parent == -1)
				break;
			check = &ActorClasses[check->parent];
		}
		else
		{
			if(state->tablepos + link.jumpOffset >= StateTable.size())
				break;
			return &StateTable[state->tablepos + link.jumpOffset];
		}
	}
	while(true);

	return nullptr;
}

// Lookup label by name.
const State *FindState(const Actor &actor, const char* label)
{
	StateLink link;
	link.jumpState = label;
	return FindState(actor, link);
}

// Expand DECORATE states into the global state table.
void InstallStates(Actor &actor)
{
	auto lbl = actor.labels.begin();
	auto lastlbl = actor.labels.end();
	FrameLabel flabel;

	std::vector<Resolve> gotoResolves;

	int lblStart = StateTable.size();
	for(auto &state : actor.states)
	{
		// This little mess simply skips over aliased states (a state with
		// multiple state labels).
		if((lbl+1) != actor.labels.end())
		{
			if((lbl+1)->state == &state - &actor.states[0])
				++lbl;
			while((lbl+1) != actor.labels.end() && (lbl+1)->state == lbl->state)
				++lbl;
		}
		if(lbl != lastlbl)
		{
			lastlbl = lbl;
			flabel.Reset(actor, *lbl);
			lbl->tablepos = lblStart = StateTable.size();
		}

		State tstate;
		tstate.sprite = state.sprite;
		tstate.duration = state.duration;
		tstate.action = state.action;
		tstate.xoffset = state.xoffset;
		tstate.yoffset = state.yoffset;
		tstate.bright = state.bright;

		// Expand the frames into multiple states
		for(unsigned i = 0;i < state.frames.length();++i)
		{
			tstate.label = flabel.Assign();
			tstate.frame = state.frames[i] - 'A';
			if(i == state.frames.length()-1)
			{
				// Sequence type only needs to be handled on the last frame.
				// All other frames just go to the next.
				switch(state.next.sequence)
				{
				case SEQ_Next:
					tstate.next = StateTable.size()+1;
					break;
				case SEQ_Wait:
					tstate.next = StateTable.size();
					break;
				case SEQ_Stop:
					tstate.next = 0;
					break;
				case SEQ_Loop:
					tstate.next = lblStart;
					break;
				case SEQ_Goto:
					// Note for later
					gotoResolves.emplace_back(Resolve{(unsigned)StateTable.size(), state.next});
					break;
				}
			}
			else
				tstate.next = StateTable.size()+1;

			StateTable.push_back(tstate);
		}
	}

	// Now resolve any gotos
	for(auto &resolve : gotoResolves)
	{
		auto &state = StateTable[resolve.statenum];
		auto nextState = FindState(actor, resolve.link);
		if(nextState == nullptr)
		{
			fprintf(stderr, "Could not resolve goto %s::%s+%d\n", resolve.link.jumpClass.c_str(), resolve.link.jumpState.c_str(), resolve.link.jumpOffset);
			exit(0);
		}
		state.next = nextState - StateTable.data();
	}
}

// ----------------------------------------------------------------------------

// Parse state name (part after goto)
void ParseStateJump(Scanner &sc, DState &state)
{
	sc.MustGetToken(TK_Identifier);
	state.next.jumpState = sc->str;
	if(sc.CheckToken(TK_ScopeResolution))
	{
		state.next.jumpClass = state.next.jumpState;
		sc.MustGetToken(TK_Identifier);
		state.next.jumpState = sc->str;
	}

	if(sc.CheckToken('+'))
	{
		sc.MustGetToken(TK_IntConst);
		state.next.jumpOffset = sc->number;
	}
}

// Parse the state sequences.
void ParseActorStates(Scanner &sc, Actor &actor)
{
	std::vector<std::pair<std::string, std::string>> labels;
	DState *lastState = nullptr;

	sc.MustGetToken('{');
	while(!sc.CheckToken('}'))
	{
		do
		{
			if(!sc.CheckToken(TK_StringConst))
			{
				sc.MustGetToken(TK_Identifier);
				auto label = sc->str;
				// Reserve our control flow statements.
				if(CheckKeyword(sc, {"loop", "goto", "stop", "wait"}) >= 0 ||
					!sc.CheckToken(':'))
					break;
				else
				{
					// Got a label so store it away.
					std::string codelabel;
					if(sc.CheckToken(TK_AnnotateStart))
					{
						// In C we need to generate names for each frame, so
						// allow the user to override the auto generated name.
						sc.MustGetToken(TK_Identifier);
						codelabel = sc->str;
						sc.MustGetToken(TK_AnnotateEnd);
					}
					labels.push_back(std::pair<std::string, std::string>(label, codelabel));
				}
			}
		}
		while(true);

		DState state;
		bool useState = false;

		// Look for a control flow keyword which will apply to the previous state.
		enum { KEYWORD_Loop, KEYWORD_Goto, KEYWORD_Stop, KEYWORD_Wait };
		switch(CheckKeyword(sc, {"loop", "goto", "stop", "wait"}))
		{
		case KEYWORD_Loop:
			if(lastState)
				lastState->next.sequence = SEQ_Loop;
			else
				sc.ScriptMessage(Scanner::ERROR, "Loop found with no previous state.\n");
			break;
		case KEYWORD_Goto:
			if(lastState)
			{
				lastState->next.sequence = SEQ_Goto;
				ParseStateJump(sc, *lastState);
			}
			else
				sc.ScriptMessage(Scanner::ERROR, "Goto found with no previous state.\n");
			break;
		case KEYWORD_Stop:
			if(lastState)
				lastState->next.sequence = SEQ_Stop;
			else
				sc.ScriptMessage(Scanner::ERROR, "Stop found with no previous state.\n");
			break;
		case KEYWORD_Wait:
			if(lastState)
				lastState->next.sequence = SEQ_Wait;
			else
				sc.ScriptMessage(Scanner::ERROR, "Wait found with no previous state.\n");
			break;
		default:
			// New state found.
			useState = true;
			state.sprite = sc->str;
			ToUpper(state.sprite);

			// Register the sprite name into a table if it hasn't been already.
			if(std::find(SpriteNames.begin(), SpriteNames.end(), state.sprite) == SpriteNames.end())
				SpriteNames.push_back(state.sprite);

			if(!sc.CheckToken(TK_StringConst))
				sc.MustGetToken(TK_Identifier);
			state.frames = sc->str;
			ToUpper(state.frames);

			state.duration = GetNegativeInteger(sc);

			if(sc.CheckToken(TK_Identifier))
			{
				enum { KEYWORD_Bright, KEYWORD_Offset };
				int keyword;
				do
				{
					// Handle misc. features
					keyword = CheckKeyword(sc, {"bright", "offset"});
					switch(keyword)
					{
					case KEYWORD_Bright:
						state.bright = true;
						break;
					case KEYWORD_Offset:
						sc.MustGetToken('(');
						state.xoffset = GetNegativeInteger(sc);
						sc.MustGetToken(',');
						state.yoffset = GetNegativeInteger(sc);
						sc.MustGetToken(')');
						break;
					default:
						break;
					}
					if(keyword < 0)
						break;

					if(!sc.CheckToken(TK_Identifier))
						break;
				}
				while(true);

				// Action functions are technically ambiguous with starting the
				// next state, so we will just use a rule that all action
				// functions must be at least 5 characters long.
				if(sc->str.length() <= 4)
				{
					sc.Rewind();
					break;
				}

				auto fname = sc->str;
				auto func = std::find_if(ActionFunctions.begin(), ActionFunctions.end(),
										 [fname](const Action &check) -> bool { return stricmp(check.function.c_str(), fname.c_str()) == 0; });
				if(func == ActionFunctions.end())
					sc.ScriptMessage(Scanner::ERROR, "Unknown action function %s.\n", fname.c_str());
				else
					state.action = func - ActionFunctions.begin();

				// Parens are optional for our unparameterized functions.
				if(sc.CheckToken('('))
					sc.MustGetToken(')');
			}
			break;
		}

		// Add the state to our local state table.
		if(useState)
		{
			actor.states.push_back(state);
			lastState = &*(actor.states.end()-1);

			// Register the labels
			for(auto &label : labels)
			{
				auto existinglbl = std::find_if(actor.labels.begin(), actor.labels.end(),
												[label](const Label &lbl) { return stricmp(lbl.label.c_str(), label.first.c_str()) == 0; });
				if(existinglbl == actor.labels.end())
				{
					Label lbl;
					lbl.label = label.first;
					lbl.codelabel = label.second;
					lbl.state = actor.states.size()-1;
					actor.labels.push_back(lbl);
				}
				else
				{
					existinglbl->state = actor.states.size()-1;
					existinglbl->codelabel = label.second;
				}
			}
			labels.clear();
		}
		else
			lastState = nullptr;
	}

	// We're done so lets get the states into the global table.
	InstallStates(actor);
}

// Parse an actor property. Some properties operate on flags.
// This could probably be improved to be more scripted since the mobjtype is
// part of the info.c/info.h.
void ParseActorProperty(Scanner &sc, Actor &actor)
{
	enum {
		PROP_Health, PROP_SeeSound, PROP_ReactionTime, PROP_AttackSound,
		PROP_PainChance, PROP_PainSound, PROP_DeathSound, PROP_Speed,
		PROP_Radius, PROP_Height, PROP_Mass, PROP_Damage, PROP_RenderStyle,
		PROP_Translation
	};

	switch(CheckKeyword(sc, {"health", "seesound", "reactiontime",
		"attacksound", "painchance", "painsound", "deathsound", "speed",
		"radius", "height", "mass", "damage", "renderstyle", "translation"}))
	{
	case PROP_Health:
		sc.MustGetToken(TK_IntConst);
		actor.props.spawnhealth = sc->number;
		break;
	case PROP_ReactionTime:
		sc.MustGetToken(TK_IntConst);
		actor.props.reactiontime = sc->number;
		break;
	case PROP_PainChance:
		sc.MustGetToken(TK_IntConst);
		actor.props.painchance = sc->number;
		break;
	case PROP_Speed:
		sc.MustGetToken(TK_IntConst);
		actor.props.speed = sc->number;
		break;
	case PROP_Radius:
		sc.MustGetToken(TK_FloatConst);
		actor.props.radius = int(sc->decimal*0x10000);
		break;
	case PROP_Height:
		sc.MustGetToken(TK_FloatConst);
		actor.props.height = int(sc->decimal*0x10000);
		break;
	case PROP_Mass:
		sc.MustGetToken(TK_IntConst);
		actor.props.mass = sc->number;
		break;
	case PROP_Damage:
		actor.props.damage = GetNegativeInteger(sc);
		break;
	case PROP_SeeSound:
		sc.MustGetToken(TK_StringConst);
		actor.props.seesound = LookupSound(sc->str);
		break;
	case PROP_AttackSound:
		sc.MustGetToken(TK_StringConst);
		actor.props.attacksound = LookupSound(sc->str);
		break;
	case PROP_PainSound:
		sc.MustGetToken(TK_StringConst);
		actor.props.painsound = LookupSound(sc->str);
		break;
	case PROP_DeathSound:
		sc.MustGetToken(TK_StringConst);
		actor.props.deathsound = LookupSound(sc->str);
		break;
	case PROP_RenderStyle:
		sc.MustGetToken(TK_StringConst);
		switch(CheckKeyword(sc, {"none", "fuzzy"}))
		{
		case 0:
			actor.props.flags &= ~MF_SHADOW;
			break;
		case 1:
			actor.props.flags |= MF_SHADOW;
			break;
		default:
			sc.ScriptMessage(Scanner::ERROR, "Unknown render style '%s'.", sc->str.c_str());
			break;
		}
		break;
	case PROP_Translation:
		sc.MustGetToken(TK_IntConst);
		if(sc->number > 3)
			sc.ScriptMessage(Scanner::ERROR, "Translation out of range.\n");
		actor.props.flags = (actor.props.flags&(~(MF_TRANSLATION1|MF_TRANSLATION2)))|(sc->number*MF_TRANSLATION1);
		break;
	default:
		sc.ScriptMessage(Scanner::ERROR, "Unknown property '%s'.\n", sc->str.c_str());
		break;
	}
}

// Set or unset a given flag.
void ParseActorFlag(Scanner &sc, Actor &actor, bool set)
{
	sc.MustGetToken(TK_Identifier);
	auto fname = sc->str;
	ToUpper(fname);

	unsigned int i = 0;
	do
	{
		if(stricmp(fname.c_str(), Flags[i].name) == 0)
		{
			if(set)
				actor.props.flags |= Flags[i].flag;
			else
				actor.props.flags &= ~Flags[i].flag;
			return;
		}
	}
	while(Flags[++i].name);

	sc.ScriptMessage(Scanner::ERROR, "Unknown flag '%s'.", fname.c_str());
}

// Register an action function into our local table.
void ParseActionFunction(Scanner &sc, Actor &actor)
{
	sc.MustGetToken(TK_Identifier);
	RequireKeyword(sc, {"native"});

	sc.MustGetToken(TK_Identifier);
	ActionFunctions.emplace_back(Action{sc->str, actor.classNum});

	sc.MustGetToken('(');
	sc.MustGetToken(')');
	sc.MustGetToken(';');
}

// Parse the actor's body (properties, flags, and states).
void ParseActorBody(Scanner &sc, Actor &actor)
{
	sc.MustGetToken('{');
	while(!sc.CheckToken('}'))
	{
		if(sc.CheckToken('+'))
			ParseActorFlag(sc, actor, true);
		else if(sc.CheckToken('-'))
			ParseActorFlag(sc, actor, false);
		else
		{
			sc.MustGetToken(TK_Identifier);
			enum { KEYWORD_Action, KEYWORD_States };
			switch(CheckKeyword(sc, {"action", "states"}))
			{
			case KEYWORD_States:
				ParseActorStates(sc, actor);
				break;
			case KEYWORD_Action:
				ParseActionFunction(sc, actor);
				break;
			default:
				ParseActorProperty(sc, actor);
				break;
			}
		}
	}
}

// Parse the header of an actor.
void ParseActor(Scanner &sc)
{
	ActorClasses.emplace_back(Actor{});
	Actor &actor = *(ActorClasses.end()-1);
	actor.classNum = ActorClasses.size()-1;

	// Name and inheritance. Actors implicitly inherit from actor except for
	// actor of course.
	sc.MustGetToken(TK_Identifier);
	actor.name = sc->str;
	if(sc.CheckToken(':'))
	{
		sc.MustGetToken(TK_Identifier);

		auto parentName = sc->str;
		auto parent = std::find_if(ActorClasses.begin(), ActorClasses.end(),
								   [parentName](const Actor &other) -> bool { return stricmp(other.name.c_str(), parentName.c_str()) == 0; });
		if(parent == ActorClasses.end())
			sc.ScriptMessage(Scanner::ERROR, "Unknown parent actor '%s'.", parentName.c_str());

		actor.parent = parent - ActorClasses.begin();
		memcpy(&actor.props, &parent->props, sizeof(actor.props));
	}
	else if(actor.classNum != 0) // Inherit from Actor
		memcpy(&actor.props, &ActorClasses[0].props, sizeof(actor.props));
	else
	{
		actor.parent = -1;
		memset(&actor.props, 0, sizeof(actor.props));
	}

	// Doom editor number
	if(sc.CheckToken(TK_IntConst))
	{
		auto ednum = sc->number;
		auto conflict = std::find_if(ActorClasses.begin(), ActorClasses.end(),
									[ednum](const Actor &other) -> bool { return other.doomednum == ednum; });
		if(conflict != ActorClasses.end())
			sc.ScriptMessage(Scanner::WARNING, "DoomEdNum %d for '%s' conflicts with previously defined '%s'.", ednum, actor.name.c_str(), conflict->name.c_str());
		actor.doomednum = sc->number;
	}

	// Native actors in ZDoom are those which have C++ code.  In our case they
	// are actors that exist in DECORATE only.
	if(sc.CheckToken(TK_Identifier))
	{
		RequireKeyword(sc, {"native"});
		actor.native = true;
	}

	ParseActorBody(sc, actor);
}

// Parse a DECORATE script.
void ParseDecorate(Scanner &sc)
{
	while(sc.TokensLeft())
	{
		sc.MustGetToken(TK_Identifier);
		RequireKeyword(sc, {"actor"});
		ParseActor(sc);
	}
}

// ----------------------------------------------------------------------------

// Read the contents of a file into memory.
std::vector<char> ReadFile(const char* fname)
{
	FILE *f = fopen(fname, "rb");
	fseek(f, 0, SEEK_END);
	auto len = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<char> out(len);
	fread(out.data(), len, 1, f);
	return out;
}

// Open a script file and parse.
void ParseScript(const char* fname)
{
	std::vector<char> script(ReadFile(fname));
	Scanner sc(script.data(), script.size());
	sc.SetScriptIdentifier(fname);

	ParseDecorate(sc);
}

// Write the generated contents of info.c
void WriteInfoC(FILE *f)
{
	unsigned int i;

	i = 0;
	fprintf(f, "char *sprnames[] = {\n\t");
	for(auto &spr : SpriteNames)
	{
		fprintf(f, "\"%s\", ", spr.c_str());
		if(((++i) % 10) == 0)
			fprintf(f, "\n\t");
	}
	fprintf(f, "NULL\n};\n\n");

	fprintf(f, "state_t states[NUMSTATES] = {\n");
	for(auto &state : StateTable)
	{
		std::string funcname("NULL");
		if(state.action >= 0)
			funcname = ActionFunctions[state.action].function;

		fprintf(f, "\t{SPR_%s,%u,%d,{%s},S_%s,%d,%d},\t// S_%s\n",
				state.sprite.c_str(),
				state.frame|(state.bright ? 0x8000 : 0),
				state.duration,
				funcname.c_str(),
				StateTable[state.next].label.c_str(),
				state.xoffset,
				state.yoffset,
				state.label.c_str()
		);
	}
	fprintf(f, "};\n\n\n");

	fprintf(f, "mobjinfo_t mobjinfo[NUMOBJTYPES] = {\n");
	for(auto &actor : ActorClasses)
	{
		if(actor.native)
			continue;

		auto SpawnState = FindState(actor, "Spawn");
		if(SpawnState == NULL) SpawnState = &StateTable[0];
		auto SeeState = FindState(actor, "See");
		if(SeeState == NULL) SeeState = &StateTable[0];
		auto PainState = FindState(actor, "Pain");
		if(PainState == NULL) PainState = &StateTable[0];
		auto MeleeState = FindState(actor, "Melee");
		if(MeleeState == NULL) MeleeState = &StateTable[0];
		auto MissileState = FindState(actor, "Missile");
		if(MissileState == NULL) MissileState = &StateTable[0];
		auto DeathState = FindState(actor, "Death");
		if(DeathState == NULL) DeathState = &StateTable[0];
		auto XDeathState = FindState(actor, "XDeath");
		if(XDeathState == NULL) XDeathState = &StateTable[0];
		auto RaiseState = FindState(actor, "Raise");
		if(RaiseState == NULL) RaiseState = &StateTable[0];

		fprintf(f, "\n\t{\n");
		fprintf(f, "\t\t%d,\n", actor.doomednum);
		fprintf(f, "\t\tS_%s,\n", SpawnState->label.c_str());
		fprintf(f, "\t\t%d,\n", actor.props.spawnhealth);
		fprintf(f, "\t\tS_%s,\n", SeeState->label.c_str());
		fprintf(f, "\t\tsfx_None,\n");
		fprintf(f, "\t\t%d,\n", actor.props.reactiontime);
		fprintf(f, "\t\tsfx_None,\n");
		fprintf(f, "\t\tS_%s,\n", PainState->label.c_str());
		fprintf(f, "\t\t%d,\n", actor.props.painchance);
		fprintf(f, "\t\tsfx_None,\n");
		fprintf(f, "\t\tS_%s,\n", MeleeState->label.c_str());
		fprintf(f, "\t\tS_%s,\n", MissileState->label.c_str());
		fprintf(f, "\t\tS_%s,\n", DeathState->label.c_str());
		fprintf(f, "\t\tS_%s,\n", XDeathState->label.c_str());
		fprintf(f, "\t\tsfx_None,\n");
		fprintf(f, "\t\t%d,\n", actor.props.speed);
		fprintf(f, "\t\t%X,\n", actor.props.radius);
		fprintf(f, "\t\t%X,\n", actor.props.height);
		fprintf(f, "\t\t%d,\n", actor.props.mass);
		fprintf(f, "\t\t%d,\n", actor.props.damage);
		fprintf(f, "\t\tsfx_None,\n");
		fprintf(f, "\t\t");
		bool hadFlag = false;
		for(unsigned int i = 0;Flags[i].name;++i)
		{
			if(actor.props.flags & Flags[i].flag)
			{
				if(hadFlag)
					fprintf(f, "|");
				fprintf(f, "MF_%s", Flags[i].name);
				hadFlag = true;
			}
		}
		if(!hadFlag)
			fprintf(f, "0");
		fprintf(f, ",\n");
		fprintf(f, "\t\tS_%s,\n", RaiseState->label.c_str());
		fprintf(f, "\t},\n");
	}
	fprintf(f, "};\n");
}

int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		printf("Usage: ./declarate <script>\n");
		return 0;
	}

	ParseScript(argv[1]);

	WriteInfoC(stdout);
	return 0;
}
