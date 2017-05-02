# DECORATE Language

## 1 - Introduction

The DECORATE language is a declarative language for specifying static actor behavior as well as defining sprite animation sequences. It assumes that the engine runs at a constant tic rate and that actions should be performed by the actor on animation state transitions.

An actor is an object in the playsim of the game engine which performs actions based on an animation sequence of states.

This language can be applied to a few game engines, however this document will be defining a common API for the Doom engine. It is an exercise to the reader to evaluate and adapt the specification should it be applicable to another engine.

### 1.1 - Limitations

The DECORATE language originated from the ZDoom source port. It was developed under a lax lexar based upon that which was used for parsing scripts for Hexen. Overtime the limitations in which this lexing mode incurs have become obvious as it heavily restricts the format in which a closure extension could be added. Given that, this document defines a dialect which uses a lexar which uses more C style tokens. This would allow for more generic lexars to be used to parse the language.

In general, existing DECORATE scripts follow a consistent style guideline which could be parsed using the language defined here, however, they are not 100% compatible in either direction. For example ZDoom would parse the following although it would be considered invalid by any sane person.

	actor~:Medikit"replaces""Zombieman""600" {
		inventory "." "amount" "1"
		+"ACTOR" "." SHOOTABLE
		states {
			"Death" ".": :
				\::\["RANDOM""(""1"",""2"")"
				loop
		}
	}

In our case we would consider the above ill formed and won't compile. Likewise since the lax lexar can't distinguish between different types of tokens, the following could be unambiguously parsed using this dialect, but not with ZDoom.

	actor Example {
		states {
			Spawn:
				TNT1 A 1
					A_Look loop
		}
	}

In this case the style isn't necessarily poor, however it would be considered unusual for DECORATE code.

### 1.2 - Goals

This document will only define the subset of the language which would be common to all implementations. That is, the language define herein should be applicable to the most basic engine such as Chocolate Doom. Further extensions to the language can be defined in external specifications or amended in future versions of this document.

## 2 - Grammar

### 2.1 - Tokens

The following base tokens are defined.

>identifier := [A-Za-z_]+[A-Za-z0-9_]*<br />
>integer := [+-]?[1-9]+[0-9]* | 0[0-9]+ | 0x[0-9A-Fa-f]+<br />
>float := [+-]?[0-9]+'.'[0-9]*([eE][+-]?[0-9]+)?<br />
>string := "([^"\\]*(\\.[^"\\]*)*)"

### 2.2 - Keywords

The following identifiers are reserved for language use.

>actor goto loop native replaces states stop wait

Keywords are case insensitive.

### 2.3 - Actor Definition
>*actor-definition*:
>>*actor-header* { *actor-body<sub>opt</sub>* }

>*actor-header*:
>>actor *identifier* *inheritance-specifier<sub>opt</sub>* *replaces-specifier<sub>opt</sub>* *editor-number<sub>opt</sub>* native<sub>opt</sub>

>*inheritance-specifier*:
>>: *identifier*

>*replaces-specifier*:
>>replaces *identifier*

>*editor-number*:
>>*integer*

>*actor-body*:
>>*actor-body-statement*<br />
>>*actor-body* *actor-body-statement*

>*actor-body-statement*:
>>*property-assignment*<br />
>>*flag-combo*<br />
>>*flag-assignment*<br />
>>*states-decl*<br />

An *actor-definition* declares an new actor. The name of the actor is given in the *actor-header* and shall be unique. In the case of an name conflict, an implementation may rename the actor for compatibility, but it is not required.

An actor with an *inheritance-specifier* copies the properties, flags, and states from the specified actor and makes the specified actor its parent. The actor must be previously defined and shall not inherit from itself even if the aformentioned renaming is implemented.

If there is no *inheritance-specifier* then the actor will inherit from the root actor implicitly unless it itself is the root actor. The root actor shall be named "Actor".

The *replaces-specifier* allows an actor to wholly replace another actor. The new actor will assume the *editor-numer* of the replacee and intercept any reference to the replacee when spawning. The replacee shall not be native.

The *editor-numer* specifies a number which is used to reference this actor for static spawning in game levels. It shall be valid as a signed 16-bit integer and must be positive.

An actor marked native indicates that there is special handling for the actor within the engine. Many of these actors are abstract and may never appear within the play sim. The structure of native actors is to be zero initialized.

### 2.3.1 - Properties
>*property-assignment*:
>>*property* *property-args*

>*property*:
>>*identifier*<br />
>>*identifier* . *identifier*

>*property-args:*
>>*property-arg*<br />
>>*property-arg*, *property-args*

>*property-arg*:
>>*string*<br />
>>*integer*<br />
>>*float*

A *property-assignment* directly assigns a constant value to the actor structure. The type and number of arguments for a property depends on the property being assigned.

The list of properties that are available is dependent on the native actors in the inheritance chain. To resolve a *property* start at the nearest native parent and continue up the chain until the name is found. If the property is not found the assignment is ill formed.

Alternatively dotted syntax can be used for properties. The name before the dot should be a native actor and the name after the dot shall be a property of that actor. The actor name "Player" is an alias for "PlayerPawn". The specified actor shall be a parent of the defined actor.

In the case of duplicated assignments, the last assignment is used. As an exception to this rule, a property's semantics can call for duplicate assignments to be permitted with linked-list semantics. In this case, the first assignment overrides any assignment made by the parent.

**Note:** Due to the zero initialization of actor structures, integer/float properties default to 0 if not specified in any parent. String properties default to NULL.

### 2.3.2 - Flags
>*flag-assignment*:
>>\+ *flag*<br />
>>\- *flag*

>*flag*:
>>*identifier*<br />
>>*identifier* . *identifier*

>*flag-combo*:
>>MONSTER<br />
>>PROJECTILE

A *flag-assignment* sets or removes a flag from an actor. If the assignment begins with the + token then the specified flag is to be set. If the assignment begins with the - token then the flag is to be removed.

The resolution of a *flag* is determined by the same rules as specified in the properties section.

If there are multiple assignments for the same flag, then the last assignment is used.

The *flag-combos* set a defined set of flags. These act just as if the flags were set manually and specific flags in the combo can be unset.

### 2.3.3 - States

>*states-decl*:
>>states { *state-seq<sub>opt</sub>* }

>*states-seq*:
>>*state*<br />
>>*state* *states-seq*

>*state*:
>>*state-labels<sub>opt</sub>* *sprite-name* *frame-sequence* *duration* *frame-properties<sub>opt</sub>* *codepointer<sub>opt</sub>* *state-flow-specifier<sub>opt</sub>*<br />
>>*state-labels* stop

>*state-labels*:
>>*state-label* :<br />
>>*state-label* : *state-labels*

>*state-label*:
>>*scope-resolution<sub>opt</sub>* *state-label-name*

>*scope-resolution*:
>>*identifier* ::

>*state-label-name*:
>>*identifier*<br />
>>*identifier* . *state-label-name*

>*sprite-name*:
>>*identifier*<br />
>>*string*

>*frame-sequence*:
>>*identifier*<br />
>>*string*

>*duration*:
>>*integer*

>*frame-properties*:
>>bright<br />
>>fast<br />
>>offset ( *integer* , *integer* )

>*codepointer*:
>>*identifier*<br />
>>*identifier* ( )

>*state-flow-specifier*:
>>goto *state-label*<br />
>>loop<br />
>>stop<br />
>>wait<br />

The *states-decl* allows for the animation sequences to be defined for the actor. All states from the parent are accessible from the new actor.

There shall only be one *states-decl* in an actor definition.

The *scope-resolution* in a *state-label* shall only be valid in the context of goto. It shall be the name of one of the parents of the defined actor.

A *state-label* followed by the stop keyword indicates that in the context of this actor the given *state-label-name* will resolve to NULL even if it is defined in the parent. Unless the *scope-resolution* is explicitly provided.

The *sprite-name* shall be exactly four characters long. If a string is used instead of an identifier, only the characters A-Z, a-z, 0-9, _, [, \, and ] are considered valid.  Additionally the strings "----" and "####" are valid, but are required to have the semantics given in section 2.3.3.1.

The *frame-sequence* may be of any length, but may only contain the characters A-Z or a-z. The characters [, \, or ] may also be used if the string form use utilized. If the semantics in the section 2.3.3.1 are implemented then the # character is also valid.

If the *frame-sequence* contains more than one character, then the *state* is to be expanded into a sequence of states with all properties identical except for the frame which will be in the order given. The *state-labels* will refer to only the first state in the sequence. The *state-flow-specifier* modifies the flow of only the last state in the sequence.

The *duration* indicates the number of play sim tics that the state is to be held before going to the next state. A duration of -1 indicates infinite, and a duration of 0 indicates that only the codepointer should be executed and the following state should be used immediately. Should there be a 0 duration loop, then the result is undefined.

The *codepointer* is to be executed on the tic that the state is transitioned to. The name of a *codepointer* shall be at least 5 characters in length.

Without the *state-flow-specifier*, the state should continue on to the next state in the sequence. If the end of the states block is reached and the state does not have a -1 the result is undefined. If the *state-flow-specifier* is goto, then the next state will be set to the state referred to by the label. This label is resolved statically and does not account for inheritance. If the specifier is wait then the state will be re-entered after the duration. If the specifier is loop then the most recently declared *state-label* will be the next frame. If the specifier is stop then the actor is to be removed from the play sim.

Specifying a *state* without a previously declared *state-label* is ill formed.

A *state-label* in the context of goto is resolved by first looking up the state name as given, going up the inheritance hierarchy as lookups fail. If none of the actors contain a state with the given name, then the part after and including the final dot can be removed and the lookup recurses. If a *scope-resolution* is given then the lookup should start at the given parent instead of at the currently defined actor.

Goto resolution is to be performed after all states are parsed in order to allow state-labels defined after the goto to be referenced.

#### 2.3.3.1 - Special Sprites (Optional)

The *sprite-name* TNT1 indicates that no sprite should be rendered.

The *sprite-name* "----" indicates that this state should not change the sprite or frame from what was previously rendered. In this case the *frame-sequence* is used only as a state count and the content is unused.

The *sprite-name* "####" indicates that the sprite should not change. This is similar to "----" except that it allows for frame changes. A "#" frame indicates that the frame should be unchanged, but the sprite can be. Using these together has the same semantics as "----".

## 3 - Base Actor Hierarchy
* Actor
* * Inventory
* * * Ammo
* * * Armor
* * * * BasicArmorBonus
* * * * BasicArmorPickup
* * * BackpackItem
* * * CustomInventory
* * * Health
* * * Key
* * * MapRevealer
* * * PowerupGiver
* * * Weapon
* * * WeaponGiver
* * PlayerPawn

The basic actor hierarchy consists of native actors which provide various functions within the language. The hiearchy only needs to exist insofar as needed for performing inheritance resolution and do not need to exist outside of the DECORATE language.

In the most basic form, only the root Actor needs to be available to be a conforming implementation. Additionally, mid teir actors do not need to be implemented. For example it would be valid for Ammo to inherit from Actor if the engine doesn't support generic Inventory. In this case special care should be taken to ensure that supported properties can be identified by their usual names. Continuing the previous example, inventory.amount is usually used to indicate the amount of ammo that should be given, but ammo.amount is ill formed.

## 4 - Base Properties & Flags

## 4.1 - Base Properties
* health *integer*
* seesound *sound*
* reactiontime *integer*
* attacksound *sound*
* painchance *integer*
* painsound *sound*
* deathsound *sound*
* speed *integer*
* radius *float*
* height *float*
* mass *integer*
* damage *integer*
* activesound *sound*

Additionally weapons should recognize:

* weapon.ammotype *class<Ammo\>*

**Note:** For the *class* type the given type must be a child of the actor named in brackets. The lookup can be deferred until all actors are parsed.

**Note:** The *sound* type indicates that the property takes a string refering to a SNDINFO logical sound. A SNDINFO implementation is not required to implement DECORATE as a fixed lookup table can be used in its place.

**Note:** Health is the only property which doesn't match with vanilla Doom's internal name. This sets the spawnhealth field.

## 4.1.1 - Recognized States

As vanilla Doom implements states as a fixed set of labels, they're implemented as properties. The state labels are listed here for completeness. These should be resolved after the *state-seq* is parsed using the normal goto resolution rules.

* Spawn
* See
* Pain
* Melee
* Missile
* Death
* XDeath
* Raise

Additionally weapons recognize the following in weaponinfo_t:

* Select
* Deselect
* Ready
* Fire
* Flash

**Note:** The select state is known internally as upstate, the deselect as downstate.

# 4.2 - Base Flags

* SPECIAL
* SOLID
* SHOOTABLE
* NOSECTOR
* NOBLOCKMAP
* AMBUSH
* JUSTHIT
* JUSTATTACKED
* SPAWNCEILING
* NOGRAVITY
* DROPOFF
* PICKUP
* NOCLIP
* SLIDE
* FLOAT
* TELEPORT
* MISSILE
* DROPPED
* SHADOW
* NOBLOOD
* CORPSE
* INFLOAT
* COUNTKILL
* COUNTITEM
* SKULLFLY
* NOTDMATCH
* TRANSLATION1
* TRANSLATION2

## A - Vanilla Code Generation

This section suggests how to convert DECORATE code into the internal fixed tables.

In the most basic form, DECORATE is a declarative language for info.h/c. The root actor is based by the structure mobjinfo_t and each property applies to it. Every non-native actor then creates a new mobjtype_t.

The state sequence translates into the states table. One can simply add states to the table in the order that actors are defined. As new sprite names are discovered they would be added to sprnames and spritenum_t. The statenum_t enumeration can be generated based on the state label combined with the actor name.

**Note:** Meta data could be put in comments combined with a comment parser in order to generate the exact tables 1:1. This would be unneeded complexity for an internal DECORATE implementation.

For best compatibility the following definition for Actor can be used:

	actor Actor native
	{
		health 1000
		reactiontime 8
		radius 20
		height 16
		mass 100

		states
		{
			Spawn: /* NULL */
				TROO A -1
				wait
		}
	}

ZDoom uses TNT1 for the NULL state, however using TROO here would allow sprnames to be in the proper order.

Weapon definitions are to be split into both a normal mobj entry as above (provided that a Spawn state does exist) and a weaponinfo_t for the weaponinfo table. If vanilla ordering is to be retained, then the WeaponGiver native can be implemented in order to define the pickup as a separate actor.

All other natives will require specific work engine side.
