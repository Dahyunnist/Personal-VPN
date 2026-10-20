# GitHub authentication and release SOP

This procedure contains the steps intentionally left for a repository owner when the
development environment cannot authenticate to GitHub. Run commands from the
repository root on a trusted computer. Never paste a token, private key, certificate,
or one-time code into an issue, commit, build log, or chat.

## 1. Authenticate

Choose one method.

### GitHub CLI over HTTPS

```powershell
gh auth login --hostname github.com --git-protocol https --web
gh auth status
```

Complete the browser flow as the account that owns or can write to
`Dahyunnist/Personal-VPN`.

### Existing SSH key

```powershell
ssh -T git@github.com
```

If no trusted key exists, follow GitHub's current SSH-key documentation. Keep the
private key outside this repository and protect it with an OS credential store and a
passphrase.

## 2. Verify the target before writing

```powershell
git remote -v
git remote get-url personal-vpn
git branch --show-current
git status --short
```

The destination must be `git@github.com:Dahyunnist/Personal-VPN.git` (or its HTTPS
equivalent), and the upgrade branch must be `codex/enterprise-upgrade`. Do not push to
an unrelated `origin` remote.

## 3. Verify the local phase commits

The following five documentation/evidence commits are local and not yet pushed:

```powershell
git log --oneline personal-vpn/codex/enterprise-upgrade..HEAD
git status --short
git diff --check
```

Expected subjects are `docs: add Windows package CI failure SOP`,
`docs: record final Linux verification results`,
`docs: record Windows package verification evidence`, and
`docs: record sanitizer and fuzz verification`, plus this SOP update. Earlier
engineering and evidence commits are already on the remote branch. Keep generated
builds and packages out of Git.

## 4. Push and verify CI

The last remote Windows package job failed at `Verify package`. Inspect its log using
the next section first. Send the first error excerpt for diagnosis; after its fix is
committed locally, push all reviewed commits and wait for every CI job to pass:

```powershell
git push --set-upstream personal-vpn codex/enterprise-upgrade
gh run list --repo Dahyunnist/Personal-VPN --branch codex/enterprise-upgrade --limit 10
$runId = gh run list --repo Dahyunnist/Personal-VPN `
  --branch codex/enterprise-upgrade --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo Dahyunnist/Personal-VPN --exit-status
```

### Inspect the current Windows package failure

The public Actions status confirms that dependency installation, configuration, build,
and tests passed. The `Verify package` step failed. GitHub requires owner access to read
the detailed step log. After signing in as a repository collaborator, open:

`https://github.com/Dahyunnist/Personal-VPN/actions/runs/39358023115/job/118417280796`

Expand **Verify package**, copy the first actual error and nearby context, and share
only that excerpt for diagnosis. Do not share tokens, cookies, private keys, or unrelated
runner environment values. Alternatively, with authenticated `gh`:

```powershell
gh run view 39358023115 --repo Dahyunnist/Personal-VPN --job 118417280796 --log
```

Do not rerun the workflow until the deterministic packaging check is fixed. After the
fix, commit and push the focused change, then verify every job with `gh run watch`.

Open a pull request only after all jobs are green:

```powershell
gh pr create --repo Dahyunnist/Personal-VPN `
  --base master `
  --head codex/enterprise-upgrade `
  --title "Enterprise correctness, security, and release upgrade" `
  --body-file .github/pull_request_template.md
```

Review failures; do not rerun repeatedly to hide a deterministic failure.

## 5. Publish a versioned release

After merging and updating the local default branch:

```powershell
git switch master
git pull --ff-only personal-vpn master
git status --short
git tag -a v0.1.0 -m "Personal-VPN v0.1.0"
git push personal-vpn v0.1.0
$runId = gh run list --repo Dahyunnist/Personal-VPN `
  --workflow release.yml --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo Dahyunnist/Personal-VPN --exit-status
```

The tag must exactly match `project(PersonalVPN VERSION 0.1.0 ...)` in `CMakeLists.txt`.
The release workflow creates `.tar.gz` and `.zip` archives plus SHA-256 files.

## 6. Verify downloaded assets

```powershell
gh release download v0.1.0 --repo Dahyunnist/Personal-VPN --dir release-check
Get-ChildItem release-check
Get-FileHash release-check\personal-vpn-*.zip -Algorithm SHA256
```

Compare the computed hash with the corresponding `.sha256` file, extract into a new
directory, and confirm the Windows archive contains `personal-vpn-client.exe`,
`wintun.dll`, the example profile, Wintun license, and documentation. Perform the
equivalent `sha256sum -c` validation for the Linux archive.

## 7. Repository settings (web UI)

On the default branch, enable a ruleset that requires pull requests, blocks force
pushes and deletions, requires all CI jobs, requires the branch to be up to date, and
dismisses stale approvals. Enable secret scanning, push protection, Dependabot alerts,
and private vulnerability reporting where the repository plan supports them.

Do not mark a failing or unexecuted job as successful merely to publish a release.

## 8. Historical credential incident

Any certificate private key that ever appeared in the public history is compromised.
Revoke it at the issuing CA, issue a distinct replacement, publish the updated CRL, and
verify that no deployed host still trusts or uses the old key. This is required even if
the current branch no longer contains the file.

History rewriting is optional after rotation and must be coordinated separately: make
a fresh mirror clone, preserve a recoverable offline bundle, enumerate the exact secret
paths, use GitHub's sensitive-data-removal procedure, verify every ref locally, then
force-update only the intended repository and require all collaborators to re-clone.
Do not experiment with history-rewrite commands in this working directory, and do not
treat rewriting as a substitute for revocation.
