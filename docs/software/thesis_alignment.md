# Thesis Alignment Notes

This file collects thesis-facing notes. It is not intended to be the final thesis
text, but it can be used as source material for the chapters on objective,
methodology, implementation and discussion.

## Possible Updated Working Title

Possible title variants that include the AI aspect without losing the embedded
systems focus:

1. **Modular implementation and verification of embedded control algorithms on
   an ESP32 Smart Farm platform with AI-assisted software development**
2. **AI-assisted development of modular embedded control software using a
   dual-ESP32 Smart Farm demonstrator**
3. **Evaluation of AI-assisted workflows for modular ESP32 control software in
   an embedded Smart Farm application**

The strongest version for this project is probably variant 1: it keeps the
original technical topic and adds AI as a methodological and evaluative layer.

## Updated Research Objective

The original objective focused on the structured implementation and evaluation
of modular control and regulation algorithms on a microcontroller platform. In
the final project, the Smart Farm demonstrator mainly covers state-based control,
communication, safety handling, FreeRTOS task integration and IoT visualization.
The PI controller topic is handled separately because the Smart Farm Kit did not
provide a meaningful physical plant for closed-loop PI control.

An updated objective could be formulated as:

> The objective of this work is to design, implement and evaluate a modular
> embedded software architecture for a dual-ESP32 Smart Farm demonstrator. The
> implementation focuses on state-based control logic, safe actuator handling,
> UART-based inter-controller communication, FreeRTOS task structuring and a
> local IoT web interface. In addition, the work investigates how AI-assisted
> development workflows can support embedded software implementation, debugging
> and documentation.

## Updated Research Questions

The thesis can combine the original embedded-systems questions with an AI
workflow question:

1. How can embedded control and state-machine logic be structured modularly on a
   resource-constrained ESP32 platform?
2. How can the behavior of such modules be tested or evaluated under defined
   boundary conditions, communication failures and sensor faults?
3. Which architectural measures support safe behavior, observability and
   maintainability in a two-controller embedded system?
4. How effective are AI-assisted development workflows for implementing,
   integrating and documenting embedded software in comparison to a purely
   manual workflow?

## Methodological Fit

The implementation follows a design-build-test cycle:

1. Define use cases and technical constraints.
2. Implement the system in modular ESP-IDF/FreeRTOS components.
3. Verify pure logic modules with host-side tests where possible.
4. Validate hardware-near behavior by flashing and observing the real kit.
5. Use serial logging and Web UI output as runtime observability mechanisms.
6. Reflect on AI-assisted development workflows used during implementation.

The AI part is not just a side note. It can be treated as an additional
methodological dimension:

- GitHub issue based Claude Code workflow as a semi-autonomous agent approach.
- VS Code based Claude/Codex workflow as an interactive pair-programming
  approach.
- Human review, hardware testing and system understanding as necessary control
  mechanisms.

## Evaluation Angle for AI Use

Useful discussion dimensions:

| Dimension | GitHub Claude workflow | IDE Claude/Codex workflow |
|-----------|------------------------|---------------------------|
| Interaction model | asynchronous, issue-based | interactive, conversational |
| Context handling | depends strongly on issue quality and repo instructions | can be steered continuously |
| Branch management | many branches and merge conflicts possible | direct local integration possible |
| Token usage | potentially high and less visible | more controlled by focused prompts |
| Strength | automated issue execution, PR-like work packages | debugging, integration, explanation, documentation |
| Weakness | harder to guide once running | requires active human steering |

The key thesis argument can be balanced:

- AI accelerated implementation and helped bridge many technology areas.
- AI output still required human system understanding, hardware tests and
  integration decisions.
- Embedded software adds constraints that are difficult to validate purely from
  code, especially timing, power supply behavior, boot behavior and physical I/O.

## Non-goals and Scope Boundaries

The written thesis should clearly separate:

- Smart Farm demonstrator: state-based control, safety, communication, Web UI.
- Separate PI controller project: discrete PI algorithm and deeper formal tests.
- Hardware design: not optimized or newly developed.
- Measurement technology: not the central research focus.

This avoids the impression that every originally planned regulation topic had
to be solved inside the Smart Farm Kit.
