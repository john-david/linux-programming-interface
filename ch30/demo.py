
# demo for python implmentation
from btree import BTree

def main():
    t = BTree.initialize()
    t.add("d", "delta")
    t.add("b", "bravo")
    t.add("a", "alpha")
    t.add("c", "charlie")
    t.add("e", "echo")

    ok, v = t.lookup("c")
    print("c ->", v if ok else None)

    ok, old = t.delete("b")
    print("deleted b:", ok, "old:", old)

    t.destroy()

if __name__ == "__main__":
    main()

