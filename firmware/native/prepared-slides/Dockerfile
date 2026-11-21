FROM node:24-bookworm-slim
WORKDIR /app
ENV NODE_ENV=production HOST=0.0.0.0
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY prepare.ts server.ts service.ts ./
USER node
EXPOSE 8080
CMD ["node", "server.ts"]
