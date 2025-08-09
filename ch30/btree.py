from __future__ import annotations
import threading
from typing import Any, Optional, Tuple, Callable


class RWLock:
    """
    Reader–writer lock with writer preference to avoid writer starvation.
    Usage:
        with rw.read_lock(): ...
        with rw.write_lock(): ...
    """
    def __init__(self) -> None:
        self._mu = threading.Lock()
        self._ok_to_read = threading.Condition(self._mu)
        self._ok_to_write = threading.Condition(self._mu)
        self._active_readers = 0
        self._active_writers = 0
        self._waiting_writers = 0

    class _ReadCtx:
        def __init__(self, rw: "RWLock") -> None:
            self.rw = rw

        def __enter__(self):
            rw = self.rw
            with rw._mu:
                # If writers are active or waiting, block new readers
                while rw._active_writers or rw._waiting_writers:
                    rw._ok_to_read.wait()
                rw._active_readers += 1
            return self

        def __exit__(self, exc_type, exc, tb):
            rw = self.rw
            with rw._mu:
                rw._active_readers -= 1
                if rw._active_readers == 0:
                    rw._ok_to_write.notify()
            return False

    class _WriteCtx:
        def __init__(self, rw: "RWLock") -> None:
            self.rw = rw

        def __enter__(self):
            rw = self.rw
            with rw._mu:
                rw._waiting_writers += 1
                while rw._active_writers or rw._active_readers:
                    rw._ok_to_write.wait()
                rw._waiting_writers -= 1
                rw._active_writers = 1
            return self

        def __exit__(self, exc_type, exc, tb):
            rw = self.rw
            with rw._mu:
                rw._active_writers = 0
                # Prefer waking writers; otherwise wake all readers
                if rw._waiting_writers:
                    rw._ok_to_write.notify()
                else:
                    rw._ok_to_read.notify_all()
            return False

    def read_lock(self):  return RWLock._ReadCtx(self)
    def write_lock(self): return RWLock._WriteCtx(self)


class _Node:
    __slots__ = ("key", "value", "left", "right", "lock")

    def __init__(self, key: str, value: Any) -> None:
        self.key = key
        self.value = value
        self.left: Optional["_Node"] = None
        self.right: Optional["_Node"] = None
        self.lock = threading.Lock()


class BTree:
    """
    Thread-safe, unbalanced BST keyed by str with arbitrary values.

    API:
        t = BTree.initialize()
        t.add(key, value)          -> 0 if inserted, 1 if replaced existing
        t.delete(key)              -> (True, old_value) or (False, None)
        t.lookup(key)              -> (True, value) or (False, None)
        t.destroy(free_value=None) -> clear all nodes; optional callback per value

    Locking strategy:
        - Tree-level RWLock:
            * lookup(): read lock (many concurrent readers)
            * add/delete/destroy(): write lock (exclusive)
        - Per-node locks taken while inspecting/mutating that node
          (hand-over-hand style down the path).
    """

    def __init__(self) -> None:
        self._root: Optional[_Node] = None
        self._rw = RWLock()

    @staticmethod
    def initialize() -> "BTree":
        return BTree()

    # ---------------------------- public ops ---------------------------------

    def add(self, key: str, value: Any) -> int:
        """Insert or replace. Returns 0 if inserted, 1 if replaced."""
        with self._rw.write_lock():
            if self._root is None:
                self._root = _Node(key, value)
                return 0

            parent: Optional[_Node] = None
            cur = self._root
            cur.lock.acquire()

            try:
                while True:
                    if key == cur.key:
                        # Replace value
                        cur.value = value
                        return 1
                    go_left = key < cur.key
                    child = cur.left if go_left else cur.right
                    if child is None:
                        # Insert new node here
                        new_node = _Node(key, value)
                        if go_left:
                            cur.left = new_node
                        else:
                            cur.right = new_node
                        return 0
                    # Move down: lock child first, then release parent
                    child.lock.acquire()
                    if parent is not None:
                        parent.lock.release()
                    parent = cur
                    cur = child
            finally:
                # Release any locks still held
                cur.lock.release()
                if parent is not None:
                    parent.lock.release()

    def lookup(self, key: str) -> Tuple[bool, Optional[Any]]:
        """Return (True, value) if found, else (False, None)."""
        with self._rw.read_lock():
            cur = self._root
            while cur is not None:
                cur.lock.acquire()
                try:
                    if key == cur.key:
                        return True, cur.value
                    cur_next = cur.left if key < cur.key else cur.right
                finally:
                    cur.lock.release()
                cur = cur_next
            return False, None

    def delete(self, key: str) -> Tuple[bool, Optional[Any]]:
        """Delete key. Returns (True, old_value) if deleted, else (False, None)."""
        with self._rw.write_lock():
            parent: Optional[_Node] = None
            cur = self._root

            if cur is None:
                return False, None

            cur.lock.acquire()

            # Search down with hand-over-hand locking
            while cur is not None and key != cur.key:
                nxt = cur.left if key < cur.key else cur.right
                if nxt is None:
                    # Not found
                    cur.lock.release()
                    if parent is not None:
                        parent.lock.release()
                    return False, None
                nxt.lock.acquire()
                if parent is not None:
                    parent.lock.release()
                parent = cur
                cur = nxt

            if cur is None:
                if parent is not None:
                    parent.lock.release()
                return False, None

            old_value = cur.value

            # Case 1: at most one child
            if cur.left is None or cur.right is None:
                child = cur.left if cur.left is not None else cur.right
                if parent is None:
                    # Deleting root
                    self._root = child
                    cur.lock.release()
                else:
                    if parent.left is cur:
                        parent.left = child
                    else:
                        parent.right = child
                    # Release in a defined order; parent was locked
                    cur.lock.release()
                    parent.lock.release()
                return True, old_value

            # Case 2: two children — replace with in-order successor
            succ_parent, succ = self._find_min_locked(cur)  # both returned locked

            # Move successor's key/value up to 'cur'
            cur.key, cur.value = succ.key, succ.value

            # Splice out successor (it has no left child)
            succ_child = succ.right
            if succ_parent is cur:
                cur.right = succ_child
            else:
                succ_parent.left = succ_child

            # Release locks carefully to avoid double-release when succ_parent is cur
            succ.lock.release()
            if succ_parent is not cur:
                succ_parent.lock.release()

            # Release cur and (if exists) original search parent
            cur.lock.release()
            if parent is not None:
                parent.lock.release()

            return True, old_value

    def destroy(self, free_value: Optional[Callable[[Any], None]] = None) -> None:
        """Clear the tree (exclusive). Optionally free/dispose each value."""
        def postorder(n: Optional[_Node]) -> None:
            if n is None:
                return
            postorder(n.left)
            postorder(n.right)
            if free_value:
                try:
                    free_value(n.value)
                except Exception:
                    # Best-effort cleanup; ignore callback errors
                    pass

        with self._rw.write_lock():
            postorder(self._root)
            self._root = None

    # ---------------------------- helpers ------------------------------------

    def _find_min_locked(self, start_locked: _Node) -> Tuple[_Node, _Node]:
        """
        Find the in-order successor in start_locked.right subtree.
        Pre: write lock held; start_locked is locked; start_locked.right is not None.
        Returns (parent_locked, min_locked), both still locked on return.
        """
        parent = start_locked
        cur = start_locked.right
        assert cur is not None
        cur.lock.acquire()
        while cur.left is not None:
            nxt = cur.left
            nxt.lock.acquire()
            parent.lock.release()
            parent = cur
            cur = nxt
        return parent, cur


