import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))
vi.mock('../../src_assets/common/assets/web/fetch_utils', () => ({
  apiFetch: vi.fn(),
}))

import { apiFetch } from '../../src_assets/common/assets/web/fetch_utils'
import Games from '../../src_assets/common/assets/web/Games.vue'

beforeEach(() => vi.clearAllMocks())

/**
 * Build the `this` context expected by the Games page methods.
 *
 * @param {object} overrides Values to replace in the default context.
 * @returns {object} Method context with a stub translator.
 */
function context(overrides = {}) {
  return {
    folder: '',
    games: [],
    scanned: false,
    scanning: false,
    busy: false,
    error: '',
    status: '',
    igdbEnabled: null,
    pickerGame: null,
    coverQuery: '',
    coverCandidates: [],
    coverSearching: false,
    cover: null,
    steamGames: [],
    $t: key => key,
    ...overrides,
  }
}

describe('scan', () => {
  it('posts the trimmed folder and marks every result unselected', async () => {
    const ctx = context({ folder: '  D:/Games  ' })
    apiFetch.mockResolvedValue({
      ok: true,
      json: async () => ({ games: [{ name: 'Hades', path: 'D:/Games/Hades.exe', added: true }] }),
    })

    await Games.methods.scan.call(ctx)

    expect(apiFetch).toHaveBeenCalledOnce()
    expect(apiFetch).toHaveBeenCalledWith('./api/games/scan', expect.objectContaining({ method: 'POST' }))
    expect(JSON.parse(apiFetch.mock.calls[0][1].body)).toEqual({ path: 'D:/Games' })
    expect(ctx.games).toEqual([
      { name: 'Hades', path: 'D:/Games/Hades.exe', added: true, selected: false, cover: null },
    ])
    expect(ctx.scanned).toBe(true)
    expect(ctx.scanning).toBe(false)
  })

  it('does nothing when the folder is blank', async () => {
    const ctx = context({ folder: '   ' })

    await Games.methods.scan.call(ctx)

    expect(apiFetch).not.toHaveBeenCalled()
    expect(ctx.scanning).toBe(false)
  })

  it('shows the rate-limit message returned by the server', async () => {
    const ctx = context({ folder: 'D:/Games' })
    apiFetch.mockResolvedValue({
      ok: false,
      text: async () => 'Game scanning is rate limited; wait a few seconds and try again',
    })

    await Games.methods.scan.call(ctx)

    expect(ctx.error).toBe('games.scan_failed: Game scanning is rate limited; wait a few seconds and try again')
    expect(ctx.games).toEqual([])
    expect(ctx.scanning).toBe(false)
  })

  it('reports network failures and clears the busy flag', async () => {
    const ctx = context({ folder: 'D:/Games' })
    apiFetch.mockRejectedValue(new Error('Network unavailable'))

    await Games.methods.scan.call(ctx)

    expect(ctx.error).toBe('games.scan_failed: Network unavailable')
    expect(ctx.scanning).toBe(false)
  })
})

describe('searchCovers', () => {
  it('queries the cover endpoint with the encoded title', async () => {
    const candidates = [{ source: 'IGDB', title: 'Hades II', thumbnail: 'a.png', image: 'a.png', key: 'igdb_1' }]
    const ctx = context({ coverQuery: ' Hades II ' })
    apiFetch.mockResolvedValue({ ok: true, json: async () => ({ candidates, igdb_enabled: true }) })

    await Games.methods.searchCovers.call(ctx)

    expect(apiFetch).toHaveBeenCalledExactlyOnceWith('./api/covers/search?q=Hades%20II')
    expect(ctx.coverCandidates).toEqual(candidates)
    expect(ctx.igdbEnabled).toBe(true)
    expect(ctx.coverSearching).toBe(false)
  })

  it('skips the request for an empty query', async () => {
    const ctx = context({ coverQuery: '  ' })

    await Games.methods.searchCovers.call(ctx)

    expect(apiFetch).not.toHaveBeenCalled()
  })

  it('clears the candidates and reports a rate-limited search', async () => {
    const ctx = context({ coverQuery: 'Hades', coverCandidates: [{ key: 'stale' }] })
    apiFetch.mockResolvedValue({ ok: false, text: async () => 'Cover search is rate limited' })

    await Games.methods.searchCovers.call(ctx)

    expect(ctx.coverCandidates).toEqual([])
    expect(ctx.error).toBe('games.cover_search_failed: Cover search is rate limited')
    expect(ctx.coverSearching).toBe(false)
  })
})

describe('addGames', () => {
  it('uploads each cover and creates every application in one batch request', async () => {
    const pending = [
      {
        name: 'Hades',
        path: 'D:/Games/Hades.exe',
        cover: { key: 'igdb_1027', image: 'https://images.igdb.com/cover.png' },
      },
      { name: 'Celeste', path: 'D:/Games/Celeste.exe' },
    ]
    const ctx = context()
    apiFetch.mockImplementation(async url => {
      if (url === './api/covers/upload') {
        return { ok: true, json: async () => ({ path: 'covers/igdb_1027.png' }) }
      }
      return { ok: true, json: async () => ({ added: 2 }) }
    })

    await Games.methods.addGames.call(ctx, pending)

    expect(apiFetch).toHaveBeenCalledTimes(2)
    expect(apiFetch.mock.calls[0][0]).toBe('./api/covers/upload')
    expect(JSON.parse(apiFetch.mock.calls[0][1].body)).toEqual({
      key: 'igdb_1027',
      url: 'https://images.igdb.com/cover.png',
    })
    expect(apiFetch.mock.calls[1][0]).toBe('./api/apps/batch')
    expect(JSON.parse(apiFetch.mock.calls[1][1].body)).toEqual({
      apps: [
        { name: 'Hades', cmd: '"D:/Games/Hades.exe"', 'image-path': 'covers/igdb_1027.png' },
        { name: 'Celeste', cmd: '"D:/Games/Celeste.exe"' },
      ],
    })
    expect(pending.every(game => game.added)).toBe(true)
    expect(pending.every(game => game.selected === false)).toBe(true)
    expect(ctx.status).toBe('games.added_count')
    expect(ctx.busy).toBe(false)
  })

  it('keeps the Steam launch command and cover of a Steam game', async () => {
    const pending = [
      {
        name: 'Hades',
        app_id: 1145360,
        command: '"C:/Steam/steam.exe" -applaunch 1145360',
        cover: 'https://cdn.cloudflare.steamstatic.com/steam/apps/1145360/library_600x900_2x.jpg',
      },
    ]
    const ctx = context()
    apiFetch.mockImplementation(async url => {
      if (url === './api/covers/upload') {
        return { ok: true, json: async () => ({ path: 'covers/steam_1145360.jpg' }) }
      }
      return { ok: true, json: async () => ({ added: 1 }) }
    })

    await Games.methods.addGames.call(ctx, pending)

    expect(JSON.parse(apiFetch.mock.calls[0][1].body)).toEqual({
      key: 'steam_1145360',
      url: 'https://cdn.cloudflare.steamstatic.com/steam/apps/1145360/library_600x900_2x.jpg',
    })
    expect(JSON.parse(apiFetch.mock.calls[1][1].body)).toEqual({
      apps: [
        {
          name: 'Hades',
          cmd: '"C:/Steam/steam.exe" -applaunch 1145360',
          'image-path': 'covers/steam_1145360.jpg',
        },
      ],
    })
  })

  it('still adds the applications when a cover upload fails', async () => {
    const pending = [{ name: 'Hades', path: 'D:/Games/Hades.exe', cover: { key: 'igdb_1', image: 'a.png' } }]
    const ctx = context()
    apiFetch.mockImplementation(async url => {
      if (url === './api/covers/upload') {
        return { ok: false, text: async () => 'Failed to download cover' }
      }
      return { ok: true, json: async () => ({ added: 1 }) }
    })

    await Games.methods.addGames.call(ctx, pending)

    expect(apiFetch.mock.calls[1][0]).toBe('./api/apps/batch')
    expect(JSON.parse(apiFetch.mock.calls[1][1].body)).toEqual({
      apps: [{ name: 'Hades', cmd: '"D:/Games/Hades.exe"' }],
    })
  })

  it('does nothing when there is nothing to add', async () => {
    const ctx = context()

    await Games.methods.addGames.call(ctx, [])

    expect(apiFetch).not.toHaveBeenCalled()
    expect(ctx.busy).toBe(false)
  })

  it('reports a failed batch request and leaves the games unadded', async () => {
    const pending = [{ name: 'Hades', path: 'D:/Games/Hades.exe' }]
    const ctx = context()
    apiFetch.mockResolvedValue({ ok: false, text: async () => 'An apps array is required' })

    await Games.methods.addGames.call(ctx, pending)

    expect(ctx.error).toBe('games.add_failed: An apps array is required')
    expect(pending[0].added).toBeUndefined()
    expect(ctx.busy).toBe(false)
  })
})
