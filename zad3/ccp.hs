{-# LANGUAGE ForeignFunctionInterface #-}
{-# LANGUAGE LambdaCase #-}
module Main where

import Control.Monad
import Control.Exception
import Foreign
import Foreign.C.Types
import Foreign.C.Error
import System.Environment
import System.Posix.Error
import System.Posix.Files
import System.Posix.IO
import System.Posix.Types

data CStatfs

foreign import ccall unsafe "sys/ioctl.h ioctl" ioctl :: Fd -> CULong -> Ptr Fd -> IO CInt
foreign import ccall unsafe "sys/vfs.h fstatfs" fstatfs :: Fd -> Ptr CStatfs -> IO CInt

requireExt :: Fd -> IO ()
requireExt fd = do
    fp <- mallocForeignPtrBytes 64
    withForeignPtr fp $ \p -> do
        throwErrnoIf_ (/= 0) "fstatfs" $ fstatfs fd p
        ty <- peekByteOff p 0
        unless (ty == (0xEF53 :: CUInt)) $
            fail "Not an ext filesystem"

cow :: FilePath -> FilePath -> IO ()
cow source target = do
    fdSource <- openFd source ReadOnly Nothing defaultFileFlags
    requireExt fdSource
    bracketOnError (openFd target WriteOnly (Just stdFileMode) defaultFileFlags{exclusive=True, trunc=True})
        (const $ removeLink target) $ \fdTarget -> do
            fdPtr' <- mallocForeignPtr
            withForeignPtr fdPtr' $ \fdPtr -> do
                poke fdPtr fdTarget
                throwErrnoPathIf_ (/= 0) "ioctl" source $ ioctl fdSource 1074030105 fdPtr

main :: IO ()
main = getArgs >>= \case
    [source, target] -> cow source target
    _ -> fail "Usage: ccp SOURCE TARGET"
