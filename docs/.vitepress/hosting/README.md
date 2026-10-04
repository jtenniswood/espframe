# GitHub Pages host-level crawler policy

The documentation is published at `jtenniswood.github.io/espframe/`, a project
site on GitHub's shared `github.io` host. A `robots.txt` published by this
repository is served at `/espframe/robots.txt`; crawlers look for host policy at
`/robots.txt`. The project file therefore cannot control crawler access for
this site.

The adjacent `robots.txt` is a host-root template. To publish it, update the
`robots.txt` in the `jtenniswood.github.io` user site repository and deploy that
site. Keep the sitemap URL pointed at this project's sitemap. This host policy
is shared by projects on that hostname, so review the user-site policy before
changing it. If Espframe moves to a dedicated custom domain, publish the
template at that domain's root instead.

Search Console verification and sitemap submission require the owner's
account and are separate from publishing this repository.
