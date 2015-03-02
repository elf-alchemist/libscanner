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
#include <unordered_map>

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

// Meta information for a declared property
struct Property
{
	enum Type { TYPE_Int, TYPE_Fixed, TYPE_Sound, TYPE_Flags, TYPE_State };

	Type type;
	std::string name;
	std::string codename;
};

// Property list for actors.  Copyable, initializes to 0/empty string.
struct PropList
{
	struct Value
	{
		Value() : number(0) {}

		std::string string;
		int number;
	};

	Value &operator[](const std::string &index)
	{
		return props[index];
	}

	// Until we can declartively do flags, this will work.
	uint32_t flags;
private:
	std::unordered_map<std::string, Value> props;
};

// Our mobj information.
struct Actor
{
	Actor() : classNum(0), parent(0), doomednum(-1), native(false) {}

	std::string name;
	std::string codename;
	int classNum;
	int parent;
	int doomednum;
	bool native;

	PropList props;

	// Local tables
	std::vector<DState> states;
	std::vector<Label> labels;
	std::vector<Property> properties;
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
const State *FindState(const Actor &actor, const std::string &label)
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
	Actor *check = &actor;
	enum {
		PROP_RenderStyle, PROP_Translation
	};

	switch(CheckKeyword(sc, {"renderstyle", "translation"}))
	{
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
		do
		{
			auto propname = sc->str;
			auto prop = std::find_if(check->properties.begin(), check->properties.end(),
									 [propname](const Property &prop) { return stricmp(prop.name.c_str(), propname.c_str()) == 0; });
			if(prop != check->properties.end())
			{
				switch(prop->type)
				{
				case Property::TYPE_Int:
					actor.props[prop->name].number = GetNegativeInteger(sc);
					break;
				case Property::TYPE_Fixed:
					sc.MustGetToken(TK_FloatConst);
					actor.props[prop->name].number = sc->decimal*0x10000;
					break;
				case Property::TYPE_Sound:
					sc.MustGetToken(TK_StringConst);
					actor.props[prop->name].string = LookupSound(sc->str);
					break;
				case Property::TYPE_Flags:
				case Property::TYPE_State:
					sc.ScriptMessage(Scanner::ERROR, "Property is automatically assigned by other means.");
					break;
				}
				return;
			}

			if(check->parent == -1)
				check = &ActorClasses[check->parent];
		}
		while(true);

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

// Actor annotation defines properties, which translates to the mobj struct.
void ParseActorAnnotation(Scanner &sc, Actor &actor)
{
	while(!sc.CheckToken(TK_AnnotateEnd))
	{
		sc.MustGetToken(TK_Identifier);
		RequireKeyword(sc, {"native"});

		Property prop;
		int type;

		enum { KEYWORD_Property };
		sc.MustGetToken(TK_Identifier);
		switch(CheckKeyword(sc, {"property"}))
		{
		case KEYWORD_Property:
			sc.MustGetToken(TK_Identifier);
			
			if((type = CheckKeyword(sc, {"int", "fixed", "sound", "flags", "state"})) >= 0)
				prop.type = static_cast<Property::Type>(type);
			else
				sc.ScriptMessage(Scanner::ERROR, "Unknown type '%s'.", sc->str.c_str());

			sc.MustGetToken(TK_Identifier);
			prop.codename = prop.name = sc->str;
			// Can't nest annotations and maintain compatibility with normal
			// DECORATE parsers so use this syntax instead for code name.
			if(sc.CheckToken('<'))
			{
				sc.MustGetToken(TK_Identifier);
				prop.codename = sc->str;
				sc.MustGetToken('>');
			}
			else if(type == Property::TYPE_State)
				prop.codename += "state";
			sc.MustGetToken(';');

			actor.properties.push_back(prop);
			break;
		default:
			sc.ScriptMessage(Scanner::ERROR, "Unknown data type '%s'.", sc->str.c_str());
			break;
		}
	}
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
		else if(sc.CheckToken(TK_AnnotateStart))
			ParseActorAnnotation(sc, actor);
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
		actor.props = parent->props;
	}
	else if(actor.classNum != 0) // Inherit from Actor
		actor.props = ActorClasses[0].props;
	else
		actor.parent = -1;

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

	// Assign a name for code MT_*
	if(sc.CheckToken(TK_AnnotateStart))
	{
		sc.MustGetToken(TK_Identifier);
		actor.codename = sc->str;
		sc.MustGetToken(TK_AnnotateEnd);
	}
	else
		actor.codename = actor.name;
	ToUpper(actor.codename);
	

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
	fprintf(f, "#include \"info.h\"\n\nchar *sprnames[] = {\n\t");
	for(auto &spr : SpriteNames)
	{
		fprintf(f, "\"%s\", ", spr.c_str());
		if(((++i) % 10) == 0)
			fprintf(f, "\n\t");
	}
	fprintf(f, "NULL\n};\n\n\n");

	for(auto &af : ActionFunctions)
		fprintf(f, "void %s();\n", af.function.c_str());

	fprintf(f, "\n\nstate_t states[NUMSTATES] = {\n");
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

		fprintf(f, "\n\t{\t// MT_%s\n\t\t%d,\t// doomednum\n", actor.codename.c_str(), actor.doomednum);
		for(auto &property : ActorClasses[0].properties)
		{
			switch(property.type)
			{
			case Property::TYPE_Int:
				fprintf(f, "\t\t%d,\t// %s\n", actor.props[property.name].number, property.codename.c_str());
				break;
			case Property::TYPE_Fixed:
				fprintf(f, "\t\t%X,\t// %s\n", actor.props[property.name].number, property.codename.c_str());
				break;
			case Property::TYPE_Sound:
				fprintf(f, "\t\tsfx_None,\t// %s\n", property.codename.c_str());
				break;
			case Property::TYPE_Flags:
			{
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
				fprintf(f, ",\t// %s\n", property.codename.c_str());
				break;
			}
			case Property::TYPE_State:
			{
				auto state = FindState(actor, property.name.c_str());
				if(state == NULL) state = &StateTable[0];
				fprintf(f, "\t\tS_%s,\t// %s\n", state->label.c_str(), property.codename.c_str());
				break;
			}
			}
		}
		fprintf(f, "\t},\n");
	}
	fprintf(f, "};\n");
}

// Write the generated contents of info.h
void WriteInfoH(FILE *f)
{
	unsigned int i;

	i = 0;
	fprintf(f, "#ifndef __INFO_H__\n#define __INFO_H__\n\n#include \"d_think.h\"\n\ntypedef enum\n{\n");
	for(auto &spr : SpriteNames)
		fprintf(f, "\tSPR_%s,\n", spr.c_str());
	fprintf(f, "\tNUMSPRITES\n\n} spritenum_t;\n\ntypedef enum\n{\n");

	for(auto &state : StateTable)
		fprintf(f, "\tS_%s\n", state.label.c_str());
	fprintf(f, "\tNUMSTATES\n} statenum_t;\n\n");

	fprintf(f, "typedef struct\n{\n\tspritenum_t sprite;\n\tint frame;\n\tint tics;\n\tactionf_t action;\n\tstatenum_t nextstate;\n\tint misc1;\n\tint misc2;\n} state;\n\nextern state_t states[NUMSTATES];\nextern char *sprnames[];\n\n");

	fprintf(f, "typedef enum {\n");
	for(auto &actor : ActorClasses)
	{
		if(actor.native)
			continue;

		fprintf(f, "\tMT_%s,\n", actor.codename.c_str());
	}
	fprintf(f, "\tNUMMOBJTYPES\n\n} mobjtype_t;\n\ntypedef struct\n{\n");

	for(auto &property : ActorClasses[0].properties)
		fprintf(f, "\tint %s;\n", property.codename.c_str());
	fprintf(f, "\n} mobjinfo_t;\n\nextern mobjinfo_t mobjinfo[NUMOBJTYPES];\n\n#endif\n");
}

int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		printf("Usage: ./declarate <script>\n");
		return 0;
	}

	ParseScript(argv[1]);

	FILE *infoh = fopen("info.h", "wb");
	WriteInfoH(infoh);
	fclose(infoh);

	FILE *infoc = fopen("info.c", "wb");
	WriteInfoC(infoc);
	fclose(infoc);
	return 0;
}
