My solution for this challenge (and the intended solution) involves parsing the
randomly-generated PEGASUS program. I find the entrypoint `PC` and `DPC` values.
The value of `DPC` is also used to determine how many total grid nodes there
are. I look at the code bytes in each grid node to extract the cell's value, and
its X and Y coordinates. After getting this info from each grid cell, I recreate
the grid as a 2D Python array. Then, I run a simple DFS through this grid from
the top-left cell to the bottom-right one. Once I find a valid path, I print out
the necessary inputs to reach the target cell.
