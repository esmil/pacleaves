/*
 *  pacleaves.c
 *
 *  Copyright (c) 2022 Emil Renner Berthing
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

/* alpm */
#include <alpm.h>
#include <alpm_list.h>

#define BITS_PER_LONG (8 * sizeof(long))
#define BIT(x) (1UL << (x))

static inline int bitmap_end(int len)
{
	return len / BITS_PER_LONG + !!(len % BITS_PER_LONG);
}

static unsigned long *bitmap_new(int len)
{
	return calloc(bitmap_end(len), sizeof(unsigned long));
}

static void bitmap_free(unsigned long *bitmap)
{
	free(bitmap);
}

static void bitmap_setbit(unsigned long *bitmap, int i)
{
	bitmap[i / BITS_PER_LONG] |= BIT(i % BITS_PER_LONG);
}

static void bitmap_clearbit(unsigned long *bitmap, int i)
{
	bitmap[i / BITS_PER_LONG] &= ~BIT(i % BITS_PER_LONG);
}

static bool bitmap_empty(const unsigned long *bitmap, int len)
{
	int end = bitmap_end(len);
	int i;

	for(i = 0; i < end; i++) {
		if(bitmap[i])
			return false;
	}
	return true;
}

static void bitmap_clear(unsigned long *bitmap, int len)
{
	memset(bitmap, 0, bitmap_end(len) * sizeof(bitmap[0]));
}

typedef struct _graph_t {
	int nodes;
	int edges;
	int idx[];
} graph_t;

static graph_t *graph_alloc(int nodes, int edges)
{
	graph_t *graph = malloc(offsetof(graph_t, idx) + (2 * nodes + edges) * sizeof(graph->idx[0]));

	if(!graph) {
		return NULL;
	}
	graph->nodes = nodes;
	graph->edges = edges;
	return graph;
}

static const int *graph_edges_from(const graph_t *graph, int u)
{
	return &graph->idx[graph->idx[u]];
}

static int pkgs_index_of(const alpm_list_t *pkgs, alpm_pkg_t *pkg)
{
	const alpm_list_t *it;
	int i;

	for(i = 0, it = pkgs; it; i++, it = it->next) {
		if(it->data == pkg) {
			return i;
		}
	}
	return -1;
}

static alpm_list_t *pkgs_node_of(alpm_list_t *pkgs, alpm_pkg_t *pkg)
{
	alpm_list_t *it;
	int i;

	for(i = 0, it = pkgs; it; i++, it = it->next) {
		if(it->data == pkg) {
			return it;
		}
	}
	return NULL;
}

typedef struct _edge_t {
	int u;
	int v;
} edge_t;

static alpm_list_t *append_edge(alpm_list_t **edges, int u, int v)
{
	edge_t *edge = malloc(sizeof(*edge));

	if(!edge) {
		return NULL;
	}
	edge->u = u;
	edge->v = v;
	return alpm_list_append(edges, edge);
}

static bool append_edges(alpm_list_t **edges, int v, const alpm_list_t *deps,
		const alpm_list_t *pkgs, alpm_list_t **copy)
{
	const alpm_list_t *dep;

	for(dep = deps; dep; dep = dep->next) {
		char *depstr = alpm_dep_compute_string(dep->data);
		alpm_pkg_t *fst, *snd;
		alpm_list_t *node;
		int u;

		if(!depstr) {
			return false;
		}

		fst = alpm_find_satisfier(*copy, depstr);
		if(!fst) {
			free(depstr);
			continue;
		}

		node = pkgs_node_of(*copy, fst);
		*copy = alpm_list_remove_item(*copy, node);
		snd = alpm_find_satisfier(*copy, depstr);
		node->next = NULL;
		node->prev = node;
		*copy = alpm_list_join(node, *copy);
		free(depstr);
		if(snd) {
			continue;
		}

		u = pkgs_index_of(pkgs, fst);
		if(u == v) {
			continue;
		}

		if(!append_edge(edges, u, v)) {
			return false;
		}
	}
	return true;
}

static int edge_compare(const void *a, const void *b)
{
	const edge_t *x = a;
	const edge_t *y = b;
	int ret = x->u - y->u;

	if(ret) {
		return ret;
	}
	return x->v - y->v;
}

static graph_t *graph_build_rdepends(const alpm_list_t *pkgs, bool optdepends)
{
	alpm_list_t *copy = alpm_list_copy(pkgs);
	alpm_list_t *edges = NULL;
	const alpm_list_t *it;
	graph_t *graph;
	int nnodes, nedges;
	int i, j;

	if(pkgs && !copy)
		return NULL;

	for(i = 0, it = pkgs; it; i++, it = it->next) {
		alpm_pkg_t *pkg = it->data;

		if(!append_edges(&edges, i,
					alpm_pkg_get_depends(pkg), pkgs, &copy)) {
			alpm_list_free(copy);
			return NULL;
		}

		if(optdepends && !append_edges(&edges, i,
					alpm_pkg_get_optdepends(pkg), pkgs, &copy)) {
			alpm_list_free(copy);
			return NULL;
		}
	}
	alpm_list_free(copy);

	nnodes = alpm_list_count(pkgs);
	nedges = alpm_list_count(edges);
	edges = alpm_list_msort(edges, nedges, edge_compare);
	for(it = edges; it && it->next;) {
		alpm_list_t *next = it->next;

		if (edge_compare(it->data, next->data) == 0) {
			edges = alpm_list_remove_item(edges, next);
			free(next->data);
			free(next);
			nedges -= 1;
		} else {
			it = next;
		}
	}

	graph = graph_alloc(nnodes, nedges);
	if(!graph) {
		goto out;
	}
	for(i = 0, j = nnodes, it = edges; i < nnodes; i++) {
		graph->idx[i] = j;
		for(; it; it = it->next) {
			edge_t *edge = it->data;

			if(edge->u != i) {
				break;
			}
			graph->idx[j++] = edge->v;
		}
		graph->idx[j++] = -1;
	}

	/*
	for(i = 0, it = pkgs; it; i++, it = it->next) {
		const int *iedges = graph_edges_from(graph, i);

		for(j = *iedges++; j >= 0; j = *iedges++) {
			printf("(%3d,%3d) %s <- %s\n", i, j,
					alpm_pkg_get_name(it->data),
					alpm_pkg_get_name(alpm_list_nth(pkgs, j)->data));
		}
	}
	*/
out:
	FREELIST(edges);
	return graph;
}

static alpm_list_t *append_scc(alpm_list_t **sccs, const int *scc, int len)
{
	int *entry = malloc((len + 1) * sizeof(entry[0]));

	if(!entry) {
		return NULL;
	}
	memcpy(entry, scc, len * sizeof(entry[0]));
	entry[len] = -1;
	return alpm_list_append(sccs, entry);
}

static int sort_by_first(const void *a, const void *b)
{
	const int *x = a;
	const int *y = b;

	return x[0] - y[0];
}

static alpm_list_t *tarjan(const graph_t *rgraph, bool allcycles)
{
	int N = rgraph->nodes;
	int *stack = malloc(N * sizeof(stack[0]));
	int *estack = malloc(N * sizeof(estack[0]));
	unsigned int *rindex = calloc(N, sizeof(rindex[0]));
	unsigned long *sccredges = bitmap_new(N);
	alpm_list_t *sccs = NULL;
	unsigned int idx, uidx;
	int r = N;
	int top = 0;
	int i, j, u, v;

	if(!stack || !estack || !rindex || !sccredges) {
		goto out;
	}

	for(i = 0; i < N; i++) {
		if(rindex[i]) {
			continue;
		}
		u = i;
		j = 0;
		idx = 2;
		rindex[u] = idx;
		while(1) {
			v = graph_edges_from(rgraph, u)[j++];
			if(v >= 0) {
				if(!rindex[v]) {
					estack[top] = j;
					stack[top++] = u;
					u = v;
					j = 0;
					idx += 2;
					rindex[u] = idx;
				} else if(rindex[v] < rindex[u]) {
					rindex[u] = rindex[v] | 1U;
				}
				continue;
			}

			stack[--r] = u;
			uidx = rindex[u];
			if(!(uidx & 1U)) {
				int scc = r;

				do {
					const int *edges;
					int w;

					v = stack[r++];
					rindex[v] = ~0U;
					edges = graph_edges_from(rgraph, v);
					for(w = *edges++; w >= 0; w = *edges++) {
						bitmap_setbit(sccredges, w);
					}
				} while(r < N && uidx <= rindex[stack[r]]);

				if(!allcycles) {
					for(j = scc; j < r; j++) {
						bitmap_clearbit(sccredges, stack[j]);
					}
					if(bitmap_empty(sccredges, N)) {
						if(!append_scc(&sccs, &stack[scc], r - scc)) {
							FREELIST(sccs);
							goto out;
						}
					} else {
						bitmap_clear(sccredges, N);
					}
				} else {
					if(r - scc > 1) {
						if(!append_scc(&sccs, &stack[scc], r - scc)) {
							FREELIST(sccs);
							goto out;
						}
					}
				}

			}

			if(top == 0) {
				break;
			}
			v = u;
			u = stack[--top];
			j = estack[top];
			if(rindex[v] < rindex[u]) {
				rindex[u] = rindex[v] | 1U;
			}
		}
	}
out:
	bitmap_free(sccredges);
	free(rindex);
	free(estack);
	free(stack);
	return sccs;
}

typedef struct _config_t {
	char *root;
	char *dbpath;
	bool allcycles;
	bool optdepends;
} config_t;

static int usage(const char * const myname)
{
	printf("usage:  %s [option(s)] [operation]\n", myname);
	printf("operations:\n");
	printf("  %s {-c --cycles}     show all dependency cycles found\n", myname);
	printf("  %s {-h --help}       show this help\n", myname);

	printf("\noptions:\n");
	printf("  -o, --optdepends     treat optional dependencies as dependencies\n");
	printf("  -b, --dbpath <path>  set an alternate database location\n");
	printf("  -r, --root <path>    set an alternate installation root\n");

	return 1;
}

static int parseargs(config_t *config, int argc, char *argv[])
{
	static const struct option opts[] =
	{
		{"cycles",     no_argument,        0,  'c'},
		{"help",       no_argument,        0,  'h'},
		{"optdepends", no_argument,        0,  'o'},
		{"root",       required_argument,  0,  'r'},
		{"dbpath",     required_argument,  0,  'b'},
		{ /* sentinel */ }
	};
	const char *optstring = "chor:b:";
	int option_index = 0;
	int opt;

	/* parse operation */
	while((opt = getopt_long(argc, argv, optstring, opts, &option_index)) != -1) {
		switch(opt) {
		case 'c':
			config->allcycles = true;
			break;
		case 'h':
			return usage(argv[0]);
		case 'o':
			config->optdepends = true;
			break;
		case 'r':
			config->root = optarg;
			break;
		case 'b':
			config->dbpath = optarg;
			break;
		case '?':
			return -1;
		}
	}

	if(optind < argc) {
		fprintf(stderr, "%s: unrecognized command '%s'\n", argv[0], argv[optind]);
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	config_t config =
	{
		.root = "/",
		.dbpath = "/var/lib/pacman",
	};
	alpm_errno_t err;
	alpm_handle_t *handle;
	alpm_list_t *pkgs;
	graph_t *rgraph;
	alpm_list_t *sccs;
	const alpm_list_t *it;
	int ret;

	/* parse the command line */
	ret = parseargs(&config, argc, argv);
	if(ret != 0) {
		return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	/* initialize library */
	handle = alpm_initialize(config.root, config.dbpath, &err);
	if(!handle) {
		fprintf(stderr, "failed to initialize alpm library:\n(root: %s, dbpath: %s)\n%s\n",
				config.root, config.dbpath, alpm_strerror(err));
		if(err == ALPM_ERR_DB_VERSION) {
			fprintf(stderr, "try running pacman-db-upgrade\n");
		}
		return EXIT_FAILURE;
	}

	/* get installed packages */
	pkgs = alpm_db_get_pkgcache(alpm_get_localdb(handle));
	if(!pkgs) {
		fprintf(stderr, "error getting packages: %s\n",
				alpm_strerror(alpm_errno(handle)));
		return EXIT_FAILURE;
	}

	/* build graph of reverse dependencies */
	rgraph = graph_build_rdepends(pkgs, config.optdepends);
	if(!rgraph) {
		fprintf(stderr, "error building graph: %s\n",
				alpm_strerror(ALPM_ERR_MEMORY));
		return EXIT_FAILURE;
	}

	/* find strongly connected components without outside reverse dependencies */
	sccs = tarjan(rgraph, config.allcycles);
	sccs = alpm_list_msort(sccs, alpm_list_count(sccs), sort_by_first);

	/* display results */
	for(it = sccs; it; it = it->next) {
		const int *scc = it->data;
		int i;
		char mark = '-';

		for(i = *scc++; i >= 0; i = *scc++) {
			alpm_pkg_t *pkg = alpm_list_nth(pkgs, i)->data;

			printf("%c %s %s\n", mark,
					alpm_pkg_get_name(pkg),
					alpm_pkg_get_version(pkg));
			mark = ' ';
		}
	}

	ret = EXIT_SUCCESS;

	FREELIST(sccs);
	free(rgraph);
	if(alpm_release(handle)) {
		ret = EXIT_FAILURE;
	}
	return ret;
}
