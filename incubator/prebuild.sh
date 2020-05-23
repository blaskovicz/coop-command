#! bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

site=$(cat "$DIR/site.html")
cat > "$DIR/incubator-site-html.h" <<EOT
#ifndef INCUBATOR_SITE_HTML_H
#define INCUBATOR_SITE_HTML_H
const char *indexPage = R""""(
$site
)"""";
#endif

EOT
