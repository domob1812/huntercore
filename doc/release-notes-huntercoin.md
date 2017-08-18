# Release Notes for Huntercoin

- For versions of the client at least 0.14.99.1, the JSON object of a player
  state is slightly modified:  The objects for the states of individual
  characters are no longer part of the player-state dictionary itself, but
  are moved to a new `characters` sub-object.

  For more details, see: https://github.com/domob1812/huntercore/issues/12
