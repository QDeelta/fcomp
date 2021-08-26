module GM.Compile where

import Utils
import Type.Core
import GM.Def
import GM.Heap

type Frame = ([Int], Int)
-- bindings in stack: (top) $5, $6, $2, $3, $4, $0, $1 (bottom)
-- => fst Frame = [5, 2]
--    snd Frame = length [$5, $6] = 2

getTop :: [Int] -> Int
getTop [] = 0
getTop (m : _) = m

initialFrame :: Int -> Frame
initialFrame n = ([], n)

pushStack :: Frame -> Int -> Frame
pushStack (ms, count) n = (newMin : ms, n)
  where newMin = count + getTop ms

popStack :: Frame -> Frame
popStack ([], _) = ([], 0) -- dummy
popStack (min : rest, count) = (rest, newCount)
  where newCount = min - getTop rest

getOffset :: Frame -> Int -> Int
getOffset f@(ms, count) n =
  if n >= min then n - min else count + nextOffset
  where 
    min = getTop ms
    nextOffset = getOffset (popStack f) n

compileE :: Frame -> CoreExpr -> Code

compileE _ (GVarCE name) = [PushG name]

compileE _ (IntCE n) = [PushI n]

compileE f (LVarCE i) = [Push (getOffset f i)]

compileE f (AppCE e1 e2) =
  right ++ left ++ [MkApp]
  where
    right = compileE f e2
    left = compileE (pushStack f 1) e1

compileE f (CaseCE e brs) = 
  eCode ++ [Eval, Jump brMap]
  where
    eCode = compileE f e
    brMap = mFromList (map (compileBranch f) brs)

compileBranch :: Frame -> (Int, Int, CoreExpr) -> (Int, Code)
compileBranch f (arity, tag, body) = (tag, Split : code ++ [Slide arity])
  where code = compileE (pushStack f arity) body

type CompiledCoreFn = (String, Int, Code)

compileFn :: CoreFn -> CompiledCoreFn
compileFn (name, arity, body) =
 (name, arity, bCode ++ cleanCode)
  where
    bCode = compileE (initialFrame arity) body
    cleanCode = if arity == 0 then [Update arity, Pop arity] else [Slide (arity + 1)] -- CAF opt

compileConstr :: CoreConstr -> Code
compileConstr (name, arity, tag) =
  replicate arity (Push (arity - 1)) ++ [Pack tag arity]

compiledPrimFn :: [CompiledCoreFn] -- name, arity, code
compiledPrimFn =
  [ ("+", 2, [Push 1, Eval, Push 1, Eval, Add, Slide 2]),
    ("-", 2, [Push 1, Eval, Push 1, Eval, Sub, Slide 2]),
    ("*", 2, [Push 1, Eval, Push 1, Eval, Mul, Slide 2])
  ]

initialState :: CoreProgram -> State
initialState (constrs, fns) = ([PushG "main", Eval], [], [], initialHeap, initialGlobalM)
  where
    (heap1, gm1) = foldl allocConstr (emptyHeap, emptyMap) constrs
    (initialHeap, initialGlobalM) = foldl allocFn (heap1, gm1) (compiledPrimFn ++ map compileFn fns)

allocConstr :: (Heap Node, GlobalMap) -> CoreConstr -> (Heap Node, GlobalMap)
allocConstr (h, m) cons@(n, a, t) = (newH, newM)
  where
    (newH, addr) = hAlloc h (NGlobal a (compileConstr cons))
    newM = mInsert m (n, addr)

allocFn :: (Heap Node, GlobalMap) -> CompiledCoreFn -> (Heap Node, GlobalMap)
allocFn (h, m) f@(n, a, c) = (newH, newM)
  where
    (newH, addr) = hAlloc h (NGlobal a c)
    newM = mInsert m (n, addr)
