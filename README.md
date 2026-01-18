# Phrasing Module (Work in Progress)

![Phrasing Module panel](doc/image/Phrasing.svg)

**Phrasing Module** is an in-development VCV Rack module focused on **musical phrasing rather than note generation**.

Instead of producing pitches or step sequences, the module is intended to shape **when things happen and how present they are** — creating pauses, repeats, changes, and dynamic emphasis over time. Its primary role is to introduce *breathing* and *form* into generative or semi-generative patches.

This project is currently exploratory and evolving. Expect breaking changes.

The module is licensed under the [MIT license](./LICENSE).

---

## Concept

Most modular systems are very good at generating material, but much weaker at shaping **structure**:

- when something rests  
- when it repeats  
- when it changes  
- when one voice comes forward and others recede  

Phrasing Module is designed to sit **above** note, rhythm, or sound generation and act as a **form and presence controller**.

A primary intended use is driving **level CV inputs** on modules such as Morph 4 or VCAs, creating spotlighting, crossfades, ducking, and ensemble-style dynamics.

---

## Current Direction (Early Design)

The module is being developed around two complementary ideas:

### 1. Gate Phrasing
Incoming clocks or gates are transformed into phrased gate output that may:

- Pause (insert rests)
- Repeat (hold the current state)
- Change (force transitions)
- Extend (occasionally hold gates longer than expected)

These behaviors are probabilistic and clock-aware, rather than step-based.

### 2. Level / Presence Control
Instead of pitch CV, the module outputs **multiple continuous CV signals** intended for level control:

- Coordinated level CVs (e.g. 4 outputs)
- Smooth fades rather than hard mutes
- Focus / spotlight behavior (one voice foregrounded, others supporting)
- Ensemble vs solo balance

These outputs are intended to be patched into mixers, VCAs, or Morph 4-style macro controllers.

---

## What This Module Is *Not*

- Not a traditional step sequencer
- Not a pitch generator
- Not a pattern recorder
- Not a melody engine

Those tools already exist. This module exists to make their output **feel intentional and alive**.

---

## Status

**Very early work in progress.**

The codebase currently reuses scaffolding and tooling from an earlier generative sequencer project, but much of that logic is being removed or replaced as the phrasing model solidifies.

Expect:
- rapid refactors
- deleted features
- incomplete controls
- placeholder behavior

---

## Building

To build from source, you need a working VCV Rack development environment. Follow the official guide:

https://vcvrack.com/manual/PluginDevelopmentTutorial

Clone the repository into your Rack `plugins` directory and run:

```bash
make