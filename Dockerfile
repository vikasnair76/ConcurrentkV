FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2 && ctest --test-dir build --output-on-failure
FROM debian:bookworm-slim
RUN groupadd --gid 10001 kv && useradd --uid 10001 --gid kv --no-create-home kv && mkdir /data && chown kv:kv /data
COPY --from=build /src/build/kv_server /src/build/kv_client /usr/local/bin/
USER kv
WORKDIR /data
EXPOSE 8080
STOPSIGNAL SIGTERM
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 CMD printf 'STATS\n' | kv_client 127.0.0.1 8080 | grep -q '^STATS '
ENTRYPOINT ["kv_server"]
CMD ["--bind", "0.0.0.0", "--wal", "/data/concurrentkv.wal"]
