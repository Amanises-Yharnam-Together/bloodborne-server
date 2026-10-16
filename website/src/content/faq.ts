// The landing page's questions; also published as FAQPage structured data.

export const faq: { q: string; a: string }[] = [
  {
    q: "Do I need my own copy of Bloodborne?",
    a: "Yes. No game files are included or downloaded. You need Bloodborne for PS4 at version 1.09: a dump of the game folder with the update, or the PS4 .pkg files, which the launcher installs for you.",
  },
  {
    q: "How many players can play together?",
    a: "Up to four hunters in one team with the server netcode, which uses all of the game's remote player slots. Today's release, on the first peer-to-peer design, takes up to three.",
  },
  {
    q: "Do I need a server or a subscription?",
    a: "No subscription and no cloud. One player of the team hosts the co-op server on their own PC, next to their game; the launcher starts it. The others reach that PC over a VPN such as Radmin VPN, Tailscale or ZeroTier, or through forwarded ports.",
  },
  {
    q: "Why a server instead of peer-to-peer?",
    a: "Players already reach each other over a VPN, so peer-to-peer gains nothing there. A server keeps the team's world when the host leaves or crashes, puts every change in one order, and gives each game one link instead of a mesh. The price is one extra hop between guests: milliseconds on a VPN.",
  },
  {
    q: "Does it run on an emulator?",
    a: "No. It runs on Bloodborne PC, a native port: the PS4 game's own code runs on your CPU and its graphics go through Vulkan, with DLSS, FSR, XeSS, frame generation, HDR and ReShade available.",
  },
  {
    q: "Can players on different versions play together?",
    a: "No. Everyone in a team, and the server, runs the same release. A different build is refused with a clear message.",
  },
  {
    q: "Is it official?",
    a: "No. It is a free, open-source fan project under the GPL. It contains no game code or assets and is not affiliated with Sony Interactive Entertainment or FromSoftware.",
  },
];
