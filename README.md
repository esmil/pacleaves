# pacleaves

## Synopsis
Show a succinct list of installed packages such that removing anything from the
system (without breaking dependencies) must include at least one of them.

## Why?
The command `pacman -Qtt` lists all installed packages that no other installed
package depends on. This gives a nice overview of what is installed on the
system without flooding you with anything required by packages already shown.
This is also very helpful to figure out if anything is not be needed and can be
safely uninstalled. Unfortunately there are at least 2 cases where it doesn't
give the full picture:

- **Case 1:** The *mesa* package depends on *libglvnd* which depends on *mesa*
  in a cycle. This means once *mesa* (or *libglvnd*) is installed none of these
  packages show up on the list even if nothing else depends on them and they
  can be safely uninstalled.

- **Case 2:** The *firefox* package depends on *ttf-font* which is provided by
  *ttf-dejavu*, *ttf-liberation*, *ttf-droid* and probably many other packages.
  So if more than one of them is installed none of them show up on the list
  even though all but one of them could be safely uninstalled.

## How?
This program solves the first case by constructing the directed graph of
installed packages and their dependencies and then grouping the nodes into
[*strongly connected components*][scc] and showing all packages in components
without any outside reverse dependencies. These SCCs are generalized cycles and
most of them only contain a single package, but in the example above *mesa* and
*libglvnd* would be grouped and all of them shown if no other package depends
on any of them.

The second case is solved by only adding edges to the graph for dependencies
that are satisfied by exactly one installed package.

[scc]: https://en.wikipedia.org/wiki/Strongly_connected_component

## Building
```sh
sudo pacman -S --needed git pacman make gcc
git clone https://github.com/esmil/pacleaves.git
cd pacleaves
make
```

## License
This program uses libalpm heavily and is licensed under [GPL v2][gpl2] just like
the [pacman][] itself.

[gpl2]: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
[pacman]: https://wiki.archlinux.org/title/Pacman_development
