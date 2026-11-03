# Tikpal 开发者协议（草稿）

> **草稿，待律师定稿，现在不生效。** 这里只列要点，让受邀的开发者提前知道大概；措辞、期限、责任、适用法律都要律师定稿、Tikpal 确认以后才发正式版。
>
> - 正式版发布以前，你用开发包只受 [LICENSE](LICENSE) 约束；提交和上架只收 Tikpal 邀请的开发者（见 [AGENTS.md](AGENTS.md) 第 1 节）。
> - 正式版会写版本号和生效日期，改版时打标签，旧版留在历史里；立项单会加一项「我已读并同意开发者协议 vX」。
> - 下面的任何一条都可能在定稿时改。

签约主体：**Spatial Therapy Inc**。

## 要点

1. **代码归谁。** 你写的应用代码归你。开发包归 Tikpal，照 [LICENSE](LICENSE)。
2. **你给 Tikpal 的授权。** 你授权 Tikpal 重编你的应用、签名、在应用市场分发、给用户自动更新，并为审核和排错保留副本；Tikpal 不为此付费。授权到什么时候、下架以后怎么办，由律师定。
3. **上架和下架。** 上不上架由 Tikpal 决定。发现安全问题、违反本协议、用户投诉或法律要求时，Tikpal 可以暂停或下架；已经装在用户设备上的，可以停止更新、从应用市场撤下。尽量提前通知你，紧急的先下架后通知。
4. **云能力。** AI 对话、语音识别、语音合成、存数据等由 Tikpal 提供，按用量扣**用户账户的点数**，跟 Tikpal 自己的应用一样；每个应用有限额，立项时定。开发者现在不付费、不分成；以后可能调整，会提前通知。
5. **收费。** 现在应用都免费（`app.json` 的 `pricing` 只有 `free`）。你不在应用里另外收费，也不引导用户到别处付费。以后开放付费另定。
6. **隐私。** 应用不收集用户的个人信息；确实要收集的，立项时说明、应用页写清、得到用户同意。数据不传到 Tikpal 以外的服务器。面向儿童的应用照 Tikpal 的儿童规则。审核时 Tikpal 会处理你的 GitHub 账号、提交的代码和日志，正式版会写清楚怎么处理。审核时，Tikpal 会把你提交的源码和小程序交给它选用的 AI 模型服务商读，只为这次审核；Tikpal 只选不拿这些数据训练模型的服务。具体是哪家、存多久，正式版写清楚。
7. **不许做的。** 恶意代码；绕过签名或审核；冒充别的应用或 Tikpal；碰别的应用的数据、分区和硬件资源；擦用户数据；违法、色情、暴力、侵权的内容；未经允许录音、录像；在单子或仓库里放别人的个人信息。
8. **版本和更新。** 版本号只往上走；每一版都要重新审核；有安全问题时，Tikpal 可以要求你限期修复。
9. **不保证。** 开发包和服务按现状提供；云能力和限额可能调整，会提前通知。
10. **责任和终止。** 各自对自己的部分负责，你对应用的内容负责；任何一方都可以终止。终止以后已经上架的应用怎么处理，由律师定。
11. **适用法律。** 由律师按签约主体定。
12. **引用。** Tikpal 的隐私政策；网站的用户条款发布以后一并引用。

---

# Tikpal Developer Agreement (draft)

> **A draft, to be finalised by a lawyer; not in effect.** It lists the main points so that invited developers know
> roughly what to expect. The wording, terms, liability and governing law are settled by a lawyer and confirmed by
> Tikpal before a final version is published.
>
> - Until then, your use of the kit is governed by the [LICENSE](LICENSE) alone, and only developers Tikpal has
>   invited can submit and publish apps (section 1 of [AGENTS.md](AGENTS.md)).
> - The final version will carry a version number and an effective date, and each new version is tagged, the old ones
>   staying in the history; the Proposal form will gain an "I have read and agree to the Developer Agreement vX" box.
> - Any point below may change when the agreement is finalised.

Contracting party: **Spatial Therapy Inc**.

## Main points

1. **Who owns the code.** The code of your app is yours. The kit is Tikpal's, under the [LICENSE](LICENSE).
2. **What you allow Tikpal.** You allow Tikpal to rebuild your app, sign it, distribute it in the app store, update it
   on users' devices automatically and keep copies for review and troubleshooting, without payment for this. How long
   this lasts and what happens after an app is withdrawn is for the lawyer to settle.
3. **Publishing and withdrawal.** Tikpal decides whether an app is published. For a security problem, a breach of this
   agreement, user complaints or a legal requirement, Tikpal may suspend or withdraw it; for copies already on users'
   devices, it may stop updates and remove the app from the store. Tikpal tells you beforehand where it can, and
   afterwards in urgent cases.
4. **Cloud features.** AI conversation, speech recognition, speech synthesis, storage and the like are Tikpal's, and
   use the **points of the user's account**, as Tikpal's own apps do; each app has limits, set at its proposal.
   Developers do not pay and do not share revenue for now; this may change, with notice beforehand.
5. **Charging.** Apps are free for now (`pricing` in `app.json` takes only `free`). You do not charge in the app or
   send users elsewhere to pay. Paid apps, if they come, will be agreed separately.
6. **Privacy.** An app does not collect users' personal information; where it truly must, say so in the proposal and on
   the app's page, and ask the user's consent. Data does not go to servers other than Tikpal's. Apps for children
   follow Tikpal's rules for children. For the review Tikpal handles your GitHub account, the code you submit and your
   logs; the final version will say how. For the review, Tikpal gives the source code and mini app you submit to an AI
   model provider of its choosing to read, for that review only; Tikpal chooses only services that do not train models
   on this data. The final version will name the provider and say how long the data is kept.
7. **Not allowed.** Malicious code; getting around signing or review; posing as another app or as Tikpal; touching
   other apps' data, partitions or hardware; erasing users' data; illegal, pornographic, violent or infringing
   content; recording sound or pictures without permission; putting other people's personal information in issues or
   repositories.
8. **Versions and updates.** Version numbers only go up; every version is reviewed again; for a security problem Tikpal
   may ask for a fix by a deadline.
9. **No warranty.** The kit and the services are provided as they are; cloud features and limits may change, with
   notice beforehand.
10. **Liability and termination.** Each party answers for its own part, and you for your app's content; either party
    may end the agreement. What happens to published apps then is for the lawyer to settle.
11. **Governing law.** For the lawyer to settle, according to the contracting party.
12. **References.** Tikpal's privacy policy; the website's terms of use once they are published.
