GIT_HASH=$(git rev-parse --short HEAD)

docker build \
  --build-arg GIT_COMMIT_HASH=${GIT_HASH} \
  --build-arg BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ") \
  -t rp-qt673-x86-lin-win \
  .
