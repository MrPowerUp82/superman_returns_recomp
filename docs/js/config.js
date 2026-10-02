// Where the installer finds the port's build. The build lives on the orphan
// `builds` branch (raw.githubusercontent.com answers with CORS headers; release
// assets do not), the same zip is attached to the GitHub release.
const REPO = 'MrPowerUp82/superman_returns_recomp';

export const CONFIG = {
  repo: REPO,
  repoUrl: `https://github.com/${REPO}`,
  releasesUrl: `https://github.com/${REPO}/releases`,
  buildUrl: `https://raw.githubusercontent.com/${REPO}/builds/superman_returns_win64.zip`,
  versionUrl: `https://raw.githubusercontent.com/${REPO}/builds/version.json`,
};
