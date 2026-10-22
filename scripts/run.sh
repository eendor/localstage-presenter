#!/bin/sh
# Build if needed, then start LocalStage and open the presenter page.
cd "$(dirname "$0")/.." || exit 1
make -s || exit 1
OPEN_BROWSER=1 exec ./localstage
