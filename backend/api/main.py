"""FastAPI application entry point — app factory + lifespan-managed Redis listener.

Run from `backend/`: `uvicorn api.main:app --host 0.0.0.0 --port 8000`
"""
import asyncio
import logging
import sys
from contextlib import asynccontextmanager

from fastapi import FastAPI

from chess_db.engine import dispose as dispose_engine

from .connection_manager import ConnectionManager
from .redis_listener import run as run_redis_listener
from .routers.admin import router as admin_router
from .routers.games import router as games_router
from .routers.websocket import build_websocket_router

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    stream=sys.stdout,
)
log = logging.getLogger("api.main")


def create_app() -> FastAPI:
    manager = ConnectionManager()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.connection_manager = manager
        task = asyncio.create_task(run_redis_listener(manager), name="redis_listener")
        log.info("API starting — redis_listener task launched")
        try:
            yield
        finally:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
            log.info("API shutting down — redis_listener task cancelled")

            await dispose_engine()
            log.info("API shutting down — chess_db engine connection pool disposed")

    app = FastAPI(title="Chess Analysis System API", lifespan=lifespan)
    app.include_router(games_router)
    app.include_router(admin_router)
    app.include_router(build_websocket_router(manager))
    return app


app = create_app()
