/*
 * Minimal appstream stub for Android packaging.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

static void *const STUB_NULL = 0;

void *as_metadata_new(void)                                            { return 0; }
int   as_metadata_parse_file(void *md, void *file, int fmt, void **e) { (void)md; (void)file; (void)fmt; (void)e; return 1; }
void *as_metadata_get_component(void *md)                              { (void)md; return 0; }

const char *as_component_get_developer_name(void *c, const char *l)   { (void)c; (void)l; return 0; }
void *      as_component_get_developer(void *c)                        { (void)c; return 0; }
const char *as_component_get_id(void *c)                               { (void)c; return 0; }
void *      as_component_get_launchable(void *c, int k)                { (void)c; (void)k; return 0; }
const char *as_component_get_name(void *c)                             { (void)c; return 0; }
const char *as_component_get_project_license(void *c)                  { (void)c; return 0; }
void *      as_component_get_releases(void *c)                         { (void)c; return 0; }
void *      as_component_get_releases_plain(void *c)                   { (void)c; return 0; }
const char *as_component_get_url(void *c, int k)                       { (void)c; (void)k; return 0; }

void *      as_launchable_get_entries(void *l)                         { (void)l; return 0; }
const char *as_developer_get_name(void *d, const char *l)             { (void)d; (void)l; return 0; }

const char *as_release_get_description(void *r, const char *locale)   { (void)r; (void)locale; return 0; }
const char *as_release_get_version(void *r)                            { (void)r; return 0; }
void *      as_release_list_get_entries(void *rl)                      { (void)rl; return 0; }
