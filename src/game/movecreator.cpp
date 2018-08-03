// Copyright (C) 2016-2017 Crypto Realities Ltd

//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <game/movecreator.h>

#include <game/map.h>
#include <game/state.h>

#include <boost/graph/astar_search.hpp>
#include <boost/graph/grid_graph.hpp>
#include <boost/unordered_map.hpp>

#include <deque>

struct neighbor_iterator;

// Model of:
//  * Graph
//  * IncidenceGraph
struct Maze
{
    // Graph concept requirements
    typedef Coord                             vertex_descriptor;
    typedef std::pair<Coord, Coord>           edge_descriptor;
    typedef boost::undirected_tag             directed_category;
    typedef boost::disallow_parallel_edge_tag edge_parallel_category;
    typedef boost::incidence_graph_tag        traversal_category;

    // IncidenceGraph concept requirements
    typedef neighbor_iterator          out_edge_iterator;
    typedef int                        degree_size_type;
};

namespace boost
{
    template <> struct graph_traits<Maze>
    {
        typedef Maze G;

        typedef G::vertex_descriptor      vertex_descriptor;
        typedef G::edge_descriptor        edge_descriptor;
        typedef G::out_edge_iterator      out_edge_iterator;

        typedef G::directed_category      directed_category;
        typedef G::edge_parallel_category edge_parallel_category;
        typedef G::traversal_category     traversal_category;

        typedef G::degree_size_type       degree_size_type;

        typedef void in_edge_iterator;
        typedef void vertex_iterator;
        typedef void vertices_size_type;
        typedef void edge_iterator;
        typedef void edges_size_type;
    };
}

// IncidenceGraph concept requirements
std::pair<Maze::out_edge_iterator, Maze::out_edge_iterator> out_edges(Maze::vertex_descriptor v, const Maze & g);
Maze::degree_size_type out_degree(Maze::vertex_descriptor v, const Maze & g);
Maze::vertex_descriptor source(const Maze::edge_descriptor &e, const Maze & g);
Maze::vertex_descriptor target(const Maze::edge_descriptor &e, const Maze & g);

static inline bool WalkableCoord(int x, int y)
{
    return IsInsideMap(x, y) && IsWalkable(x, y);
}

static inline bool WalkableCoord(const Coord &c)
{
    return WalkableCoord(c.x, c.y);
}

// Iterator
struct neighbor_iterator :
    public boost::iterator_facade<neighbor_iterator,
                                  std::pair<Coord, Coord>,
                                  boost::forward_traversal_tag,
                                  std::pair<Coord, Coord>& >
{
public:
    neighbor_iterator()
    {
    }

    neighbor_iterator(const Coord &c, bool end)
        : coord(c)
    {
        if (end)
        {
            dx = -1;
            dy = 2;
        }
        else
        {
            dy = -2;
            dx = 1;
            increment();
        }
    }

    neighbor_iterator &operator=(const neighbor_iterator &that)
    {
        coord = that.coord;
        dx = that.dx;
        dy = that.dy;
        return *this;
    }

    std::pair<Coord, Coord> operator*() const
    {
        return std::make_pair(coord, Coord(coord.x + dx, coord.y + dy));
    }

    void operator++()
    {
        increment();
    }

    bool operator==(neighbor_iterator const& that) const
    {
        return coord == that.coord && dx == that.dx && dy == that.dy;
    }

    bool equal(neighbor_iterator const& that) const { return operator==(that); }

    void increment()
    {
        for (;;)
        {
            dx++;
            if (dx == 2)
            {
                dx = -1;
                dy++;
            }
            else if (dx == 0 && dy == 0)
                continue;

            if (dy >= 2)
                return;
            if (WalkableCoord(coord.x + dx, coord.y + dy))
                return;
        }
    }

private:
    Coord coord;
    int dx, dy;
};

std::pair<Maze::out_edge_iterator, Maze::out_edge_iterator> out_edges(Maze::vertex_descriptor v, const Maze & g)
{
    return std::make_pair(
        Maze::out_edge_iterator(v, false),
        Maze::out_edge_iterator(v, true) );
}

Maze::degree_size_type out_degree(Maze::vertex_descriptor v, const Maze & g)
{
    std::pair<Maze::out_edge_iterator, Maze::out_edge_iterator> iters = out_edges(v, g);
    int deg = 0;
    while (iters.first != iters.second)
    {
        iters.first++;
        deg++;
    }
    return deg;
}

Maze::vertex_descriptor source(const Maze::edge_descriptor &e, const Maze & g)
{
    return e.first;
}

Maze::vertex_descriptor target(const Maze::edge_descriptor &e, const Maze & g)
{
    return e.second;
}

// Goal visitor and heuristic functor
class MazeGoal : public boost::default_astar_visitor,
                 public boost::astar_heuristic<Maze, int>
{
public:
    MazeGoal(const Coord &goal_) : goal(goal_)
    {
    }

    // Exception thrown when the goal vertex is found
    struct found_goal {};

    // Vertex visitor
    void examine_vertex(const Coord &v, const Maze&) const
    {
        if (v == goal)
            throw found_goal();
    }

    // Heuristic
    int operator()(const Coord &v)
    {
        return distLInf(v, goal);
    }

private:
    Coord goal;
};

// A hash function for vertices.
struct vertex_hash : public std::unary_function<Coord, std::size_t>
{
    std::size_t operator()(const Coord &c) const
    {
        return (c.x << 16) | c.y;
    }
};

template <typename K, typename V>
class default_map
{
public:
    typedef K key_type;
    typedef V data_type;
    typedef std::pair<K, V> value_type;

    default_map(const V &defaultValue_)
        : defaultValue(defaultValue_)
    {
    }

    V & operator[](K const& k)
    {
        typename std::map<K, V>::iterator mi = m.find(k);
        if (mi != m.end())
            return mi->second;
        mi = m.insert(value_type(k, defaultValue)).first;
        return mi->second;
    }

private:
    std::map<K, V> m;
    V const defaultValue;
};

// Helper function for creating waypoints (linear path segments)
bool CheckLinearPath(const Coord &start, const Coord &target)
{
    CharacterState tmp;
    tmp.from = tmp.coord = start;
    tmp.waypoints.push_back(target);
    while (!tmp.waypoints.empty())
        tmp.MoveTowardsWaypoint();
    return tmp.coord == target;
}

std::vector<Coord> FindPath(const Coord &startPt, const Coord &goal)
{
    std::vector<Coord> waypoints;

    if (!WalkableCoord(startPt) || !WalkableCoord(goal))
        return waypoints;

    boost::static_property_map<int> weight(1);

    // The predecessor map is a vertex-to-vertex mapping.
    typedef boost::unordered_map<Coord, Coord, vertex_hash> pred_map;
    pred_map predecessor;
    boost::associative_property_map<pred_map> pred_pmap(predecessor);

    typedef boost::associative_property_map< default_map<Coord, int> > DistanceMap;
    typedef default_map<Coord, int> WrappedDistanceMap;
    WrappedDistanceMap wrappedMap = WrappedDistanceMap(std::numeric_limits<int>::max());
    wrappedMap[startPt] = 0;
    DistanceMap d = DistanceMap(wrappedMap);

    MazeGoal maze_goal(goal);

    std::map<Coord, int> index_map, rank_map;
    std::map<Coord, boost::default_color_type> color_map;

    bool found = false;

    try
    {
        astar_search_no_init(
                Maze(), startPt, maze_goal,
                boost::weight_map(weight)
                    .predecessor_map(pred_pmap)
                    .distance_map(d)
                    .visitor(maze_goal)
                    .vertex_index_map(boost::associative_property_map< std::map<Coord, int> >(index_map))
                    .rank_map(boost::associative_property_map< std::map<Coord, int> >(rank_map))
                    .color_map(boost::associative_property_map< std::map<Coord, boost::default_color_type> >(color_map))
                    .distance_compare(std::less<int>())
                    .distance_combine(std::plus<int>())
            );
    }
    catch (MazeGoal::found_goal fg)
    {
        found = true;
    }

    if (!found)
        return waypoints;

    // Walk backwards from the goal through the predecessor chain adding
    // vertices to the solution path.
    std::deque<Coord> solution;
    for (Coord u = goal; u != startPt; u = predecessor[u])
        solution.push_front(u);

    // Generate waypoints by linearizing parts of path
    waypoints.push_back(startPt);
    while (!solution.empty())
    {
        // Find prefix of solution that can be linearized

        // Binary search
        int start = 0;
        int end = solution.size();
        while (start < end - 1)
        {
            int mid = (start + end) / 2;
            if (CheckLinearPath(waypoints.back(), solution[mid]))
                start = mid;
            else
                end = mid;
        }
        solution.erase(solution.begin(), solution.begin() + start);
        waypoints.push_back(solution.front());
        solution.pop_front();
    }

    return waypoints;
}
