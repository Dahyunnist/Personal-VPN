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

The engineering/release and documentation/evidence phases were committed locally
without contacting GitHub. Confirm both subjects are present and the worktree is clean:

```powershell
git log -5 --oneline
git status --short
git diff --check
```

Expected subjects are `build: add fuzzing benchmarks and release packages` and
`docs: add architecture security and project evidence`. If final verification produces
a necessary fix, review its diff and create a third focused commit; never fold a secret,
build output, or unrelated file into it.

## 4. Push and verify CI

```powershell
git push --set-upstream personal-vpn codex/enterprise-upgrade
gh run list --repo Dahyunnist/Personal-VPN --branch codex/enterprise-upgrade --limit 10
$runId = gh run list --repo Dahyunnist/Personal-VPN `
  --branch codex/enterprise-upgrade --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo Dahyunnist/Personal-VPN --exit-status
```

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
