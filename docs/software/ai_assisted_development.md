# AI-Assisted Development Notes

This project used AI tools not only for isolated code snippets but as part of
the development workflow. The experience can be discussed as a methodological
component of the bachelor thesis.

## Used AI Workflows

### GitHub-based Claude Code workflow

Claude Code was connected to the GitHub repository through a workflow and a
secret token. Issues could be assigned to Claude by mentioning it in the issue.
Claude then worked on the issue and produced code changes on a branch.

Observed strengths:

- useful for isolated, well-scoped work packages,
- can operate asynchronously,
- produces branch-based changes that can theoretically be reviewed like normal
  pull requests,
- shows how agentic coding workflows may be used in professional repositories.

Observed weaknesses:

- quality depends heavily on issue wording and repository instructions,
- generated branches can become difficult to merge if tasks overlap,
- token consumption can be high,
- the workflow feels less suitable for exploratory embedded debugging,
- hardware-specific issues still require local testing and human interpretation.

### IDE-based Claude/Codex workflow

Claude Code and Codex were also used directly inside the local development
environment. In this mode, the AI assistant could inspect the workspace,
explain code, propose changes, run builds and react to hardware test feedback.

Observed strengths:

- better for iterative debugging and integration,
- easier to steer step by step,
- useful for documentation extraction and diagram planning,
- fits well with real hardware testing because observations can immediately be
  fed back into the coding process,
- more transparent for learning and system understanding.

Observed weaknesses:

- still requires careful review,
- can introduce plausible but wrong assumptions,
- may need several iterations for hardware-specific effects,
- depends strongly on the developer's ability to describe symptoms and judge
  proposed changes.

## Role of Human System Understanding

The project experience suggests that AI can generate and integrate code quickly,
but embedded systems still need human supervision in areas such as:

- wiring and power supply,
- boot and reset behavior,
- physical actuator behavior,
- sensor plausibility,
- timing-sensitive protocols,
- deciding whether a software fix or hardware fix is appropriate.

The developer's system understanding becomes less about typing every line of
code manually and more about:

- decomposing problems,
- checking assumptions,
- validating behavior on the real system,
- deciding which AI output is useful,
- integrating generated code into a coherent architecture.

## Suggested Discussion Statement

The thesis can argue that AI-assisted development substantially accelerates
embedded software implementation, especially when many technologies must be
combined quickly. However, the approach does not remove the need for engineering
judgment. In embedded projects, compilation success is not sufficient; generated
software must be validated against timing, wiring, physical actuator behavior
and safety requirements.

## Comparison Table

| Criterion | GitHub agent workflow | IDE assistant workflow |
|-----------|----------------------|------------------------|
| Best use case | isolated issue implementation | iterative development and debugging |
| Feedback speed | slower, branch/PR based | immediate, conversational |
| Merge complexity | can be high with overlapping branches | lower if integrated locally |
| Hardware debugging | indirect | strong, because observations can guide the next change |
| Documentation extraction | possible | very effective |
| Required human role | reviewer and integrator | active co-developer and system validator |
