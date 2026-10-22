# Security policy

rct3++ reads files it cannot trust: game archives, saves, scenarios and parks that people share. A file
that makes it crash, hang, read or write out of bounds, or use unbounded memory is a security problem.

## Reporting

Report it privately, not in a public issue:

- through GitHub's [private vulnerability reporting](https://github.com/tomast1337/rct3pp/security/advisories/new), or
- by email to vycasnicolas@gmail.com.

Say which version or commit you used, what happens, and how to reproduce it. If the problem needs a
specific file, describe how to make one rather than sending a game file; a small file you built yourself,
or your own save, is fine.

You should hear back within a week. Once a fix is out, we credit the reporter unless they ask us not to.

## Supported versions

rct3++ has no releases yet. Fixes go to the `main` branch only.
