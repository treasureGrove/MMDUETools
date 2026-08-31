# AI Model Routing

This repository uses the following routing rules for any future AI/LLM task selection.

## Selection flow

1. Identify the task type and risk.
2. Check special-domain rules first.
3. Use the level table for normal coding work.
4. If multiple models fit, choose the cheapest one that is still safe.

## Level routing table

| Level | Task characteristics | Model |
|---|---|---|
| Level 0 | Single-file, low-risk, no architecture impact | DeepSeek V4 Pro |
| Level 1 | Bug fix, formatting, comments, small functions, clear scope | DeepSeek V4 Pro |
| Level 2 | New feature, multi-file change, medium complexity | GLM-5.3 |
| Level 3 | Complex engineering, multi-module, architecture impact, high risk | Claude Opus → Claude Fable 5 → GLM-5.3 → DeepSeek V4 Pro |
| Level 4 | Critical architecture, hard to reverse, new technical direction | Claude Fable 5 → GPT-5.6 Sol → Claude Opus → GLM-5.3 → DeepSeek V4 Pro |

## Special routing

| Domain / task | Design / analysis | Implementation | Review |
|---|---|---|---|
| Rendering / Shader / HLSL / BRDF / Lighting / GPU / GBuffer / Deferred | GPT-5.6 Sol | DeepSeek V4 Pro | Claude Opus |
| Unreal Engine architecture | Claude Fable 5 | GLM-5.3 | Claude Opus |
| Unreal Engine bug fixing | Claude Opus | DeepSeek V4 Pro | Claude Opus |
| Unity new systems | GLM-5.3 | GLM-5.3 | Claude Opus |
| Unity bug fixing | DeepSeek V4 Pro | DeepSeek V4 Pro | Claude Opus |
| Optimization / performance analysis | GPT-5.6 Sol | DeepSeek V4 Pro | Claude Opus |
| Large document / paper analysis | Kimi K3 | Kimi K3 | Claude Opus |

## Default priority order

### Normal coding
1. DeepSeek V4 Pro
2. GLM-5.3
3. Claude Opus
4. Claude Fable 5

### Architecture
1. Claude Fable 5
2. GPT-5.6 Sol
3. Claude Opus

### Debugging
1. DeepSeek V4 Pro
2. Claude Opus
3. GPT-5.6 Sol

## Cost control rules

- Do not use Claude Fable 5, GPT-5.6 Sol, or Claude Opus for simple fixes, formatting, comments, or small functions.
- Prefer DeepSeek V4 Pro unless the task clearly needs higher reasoning depth, architecture judgment, or risk review.
