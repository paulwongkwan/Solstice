<template>
  <div>
    <Navbar/>
    <div class="container mt-4">
      <h2>{{ $t('games.title') }}</h2>
      <p class="text-muted">{{ $t('games.description') }}</p>

      <div class="input-group mb-3">
        <label for="games-folder" class="visually-hidden">{{ $t('games.folder') }}</label>
        <input id="games-folder" type="text" class="form-control monospace" v-model="folder"
               :placeholder="$t('games.folder_placeholder')" @keyup.enter="scan">
        <button class="btn btn-primary" type="button" @click="scan" :disabled="scanning || !folder.trim()">
          <Search :size="16" class="icon"></Search>
          {{ scanning ? $t('games.scanning') : $t('games.scan') }}
        </button>
      </div>

      <div v-if="error" class="alert alert-danger">{{ error }}</div>
      <div v-if="status" class="alert alert-success">{{ status }}</div>
      <div v-if="scanned && games.length === 0" class="alert alert-info">{{ $t('games.no_results') }}</div>

      <div v-if="games.length" class="card">
        <div class="card-body">
          <div class="d-flex flex-wrap justify-content-between align-items-center gap-2 mb-3">
            <div class="form-check">
              <input id="games-select-all" class="form-check-input" type="checkbox" :checked="allSelected"
                     @change="toggleAll">
              <label class="form-check-label" for="games-select-all">{{ $t('games.select_all') }}</label>
            </div>
            <div class="d-flex flex-wrap gap-2">
              <button class="btn btn-secondary" type="button" @click="findCoversForSelection"
                      :disabled="busy || !selectedGames.length">
                <ImageIcon :size="16" class="icon"></ImageIcon>
                {{ $t('games.auto_covers') }}
              </button>
              <button class="btn btn-primary" type="button" @click="addSelected" :disabled="busy || !selectedGames.length">
                <Plus :size="16" class="icon"></Plus>
                {{ $t('games.add_selected') }}
              </button>
            </div>
          </div>

          <div class="table-responsive">
            <table class="table align-middle">
              <thead>
                <tr>
                  <th scope="col"><span class="visually-hidden">{{ $t('games.select') }}</span></th>
                  <th scope="col">{{ $t('games.cover') }}</th>
                  <th scope="col">{{ $t('games.name') }}</th>
                  <th scope="col" class="d-none d-lg-table-cell">{{ $t('games.path') }}</th>
                  <th scope="col"><span class="visually-hidden">{{ $t('games.actions') }}</span></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="game in games" :key="game.path">
                  <td>
                    <input class="form-check-input" type="checkbox" v-model="game.selected" :aria-label="game.name">
                  </td>
                  <td>
                    <img v-if="game.cover" :src="game.cover.thumbnail" class="games-cover" alt="">
                    <div v-else class="games-cover games-cover-empty" aria-hidden="true"></div>
                  </td>
                  <td>
                    <input class="form-control form-control-sm" v-model="game.name" :aria-label="$t('games.name')">
                    <span v-if="game.added" class="badge bg-secondary mt-1">{{ $t('games.already_added') }}</span>
                    <span v-if="game.cover" class="badge bg-success mt-1 ms-1">{{ game.cover.source }}</span>
                  </td>
                  <td class="d-none d-lg-table-cell"><code class="small">{{ game.path }}</code></td>
                  <td class="text-end">
                    <button class="btn btn-sm btn-outline-secondary" type="button" @click="openPicker(game)">
                      {{ game.cover ? $t('games.change_cover') : $t('games.find_cover') }}
                    </button>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>

          <p v-if="igdbEnabled === false" class="text-muted small mb-0">{{ $t('games.igdb_hint') }}</p>
        </div>
      </div>
    </div>

    <div class="modal fade" ref="pickerModal" tabindex="-1" aria-hidden="true">
      <div class="modal-dialog modal-lg modal-dialog-scrollable">
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">{{ $t('games.cover_picker_title', { name: pickerGame ? pickerGame.name : '' }) }}</h5>
            <button class="btn-close" type="button" @click="closePicker" :aria-label="$t('_common.close')"></button>
          </div>
          <div class="modal-body">
            <div class="input-group mb-3">
              <label for="games-cover-query" class="visually-hidden">{{ $t('games.cover_search') }}</label>
              <input id="games-cover-query" type="text" class="form-control" v-model="coverQuery"
                     :placeholder="$t('games.cover_search')" @keyup.enter="searchCovers">
              <button class="btn btn-secondary" type="button" @click="searchCovers" :disabled="coverSearching">
                <Search :size="16" class="icon"></Search>
              </button>
            </div>

            <div v-if="coverSearching" class="text-center py-3">
              <output class="spinner-border spinner-border-sm">
                <span class="visually-hidden">{{ $t('_common.loading') }}</span>
              </output>
            </div>
            <div v-else-if="coverCandidates.length === 0" class="text-muted text-center py-3">
              {{ $t('games.cover_empty') }}
            </div>
            <div v-else class="games-cover-grid">
              <button v-for="candidate in coverCandidates" :key="candidate.key" type="button"
                      class="games-cover-option" :class="{ selected: cover === candidate }"
                      @click="chooseCover(candidate)">
                <img :src="candidate.thumbnail" :alt="candidate.title" loading="lazy">
                <span class="games-cover-title">{{ candidate.title }}</span>
                <span class="badge bg-secondary">{{ candidate.source }}</span>
              </button>
            </div>
          </div>
          <div class="modal-footer">
            <button class="btn btn-secondary" type="button" @click="closePicker">{{ $t('_common.close') }}</button>
            <button class="btn btn-primary" type="button" @click="applyCover" :disabled="!cover">
              {{ $t('games.use_cover') }}
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { Modal } from 'bootstrap/dist/js/bootstrap'
import { Image as ImageIcon, Plus, Search } from '@lucide/vue'
import Navbar from './Navbar.vue'
import { apiFetch } from './fetch_utils.js'

export default {
  components: {
    ImageIcon,
    Navbar,
    Plus,
    Search,
  },
  data() {
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
      pickerModal: null,
      coverQuery: '',
      coverCandidates: [],
      coverSearching: false,
      cover: null,
    }
  },
  computed: {
    selectedGames() {
      return this.games.filter(game => game.selected)
    },
    allSelected() {
      return this.games.length > 0 && this.games.every(game => game.selected)
    },
  },
  methods: {
    /**
     * Scan the configured folder for game executables.
     *
     * @returns {Promise<void>}
     */
    async scan() {
      this.error = ''
      this.status = ''
      if (!this.folder.trim()) {
        return
      }

      this.scanning = true
      try {
        const response = await apiFetch('./api/games/scan', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path: this.folder.trim() }),
        })
        if (!response.ok) {
          throw new Error(await response.text())
        }
        const result = await response.json()
        this.games = (result.games || []).map(game => ({
          ...game,
          selected: false,
          cover: null,
        }))
        this.scanned = true
      } catch (e) {
        this.error = `${this.$t('games.scan_failed')}: ${e.message}`
      } finally {
        this.scanning = false
      }
    },
    /**
     * Select or clear every discovered game.
     */
    toggleAll() {
      const select = !this.allSelected
      this.games.forEach(game => {
        game.selected = select
      })
    },
    /**
     * Open the cover picker for a game.
     *
     * @param {object} game Game to find a cover for.
     */
    openPicker(game) {
      this.pickerGame = game
      this.coverQuery = game.name
      this.cover = game.cover
      this.coverCandidates = game.cover ? [game.cover] : []
      this.pickerModal = Modal.getOrCreateInstance(this.$refs.pickerModal)
      this.pickerModal.show()
      if (!game.cover) {
        this.searchCovers()
      }
    },
    /**
     * Close the cover picker.
     */
    closePicker() {
      if (this.pickerModal) {
        this.pickerModal.hide()
      }
      this.pickerGame = null
    },
    /**
     * Search IGDB and the Steam store for the current query.
     *
     * @returns {Promise<void>}
     */
    async searchCovers() {
      if (!this.coverQuery.trim()) {
        return
      }

      this.coverSearching = true
      try {
        const response = await apiFetch(`./api/covers/search?q=${encodeURIComponent(this.coverQuery.trim())}`)
        if (!response.ok) {
          throw new Error(await response.text())
        }
        const result = await response.json()
        this.igdbEnabled = result.igdb_enabled
        this.coverCandidates = result.candidates || []
      } catch (e) {
        this.coverCandidates = []
        this.error = `${this.$t('games.cover_search_failed')}: ${e.message}`
      } finally {
        this.coverSearching = false
      }
    },
    /**
     * Mark a candidate as the picked cover.
     *
     * @param {object} candidate Cover candidate.
     */
    chooseCover(candidate) {
      this.cover = candidate
    },
    /**
     * Apply the picked cover to the game being edited.
     */
    applyCover() {
      if (this.pickerGame && this.cover) {
        this.pickerGame.cover = this.cover
      }
      this.closePicker()
    },
    /**
     * Fetch the best cover for every selected game that does not have one yet.
     *
     * @returns {Promise<void>}
     */
    async findCoversForSelection() {
      this.busy = true
      this.error = ''
      try {
        for (const game of this.selectedGames) {
          if (game.cover) {
            continue
          }
          const response = await apiFetch(`./api/covers/search?q=${encodeURIComponent(game.name)}`)
          if (!response.ok) {
            continue
          }
          const result = await response.json()
          this.igdbEnabled = result.igdb_enabled
          const candidates = result.candidates || []
          const preferred = candidates.find(candidate => candidate.source === 'IGDB') || candidates[0]
          if (preferred) {
            game.cover = preferred
          }
        }
      } finally {
        this.busy = false
      }
    },
    /**
     * Add every selected game as an application.
     *
     * @returns {Promise<void>}
     */
    async addSelected() {
      this.busy = true
      this.error = ''
      let added = 0
      try {
        for (const game of this.selectedGames) {
          if (game.added) {
            continue
          }

          let imagePath = null
          if (game.cover) {
            const upload = await apiFetch('./api/covers/upload', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ key: game.cover.key, url: game.cover.image }),
            })
            if (upload.ok) {
              const result = await upload.json()
              imagePath = result.path
            }
          }

          const app = {
            name: game.name,
            cmd: `"${game.path}"`,
            index: -1,
          }
          if (imagePath) {
            app['image-path'] = imagePath
          }

          const response = await apiFetch('./api/apps', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(app),
          })
          if (!response.ok) {
            throw new Error(await response.text())
          }

          game.added = true
          game.selected = false
          added++
        }
        this.status = this.$t('games.added_count', { count: added })
      } catch (e) {
        this.error = `${this.$t('games.add_failed')}: ${e.message}`
      } finally {
        this.busy = false
      }
    },
  },
}
</script>

<style scoped>
.games-cover {
  width: 48px;
  height: 72px;
  object-fit: cover;
  border-radius: 6px;
  background-color: var(--bs-secondary-bg);
}

.games-cover-empty {
  border: 1px dashed var(--bs-border-color);
}

.games-cover-grid {
  display: grid;
  gap: 0.75rem;
  grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
}

.games-cover-option {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.35rem;
  padding: 0.5rem;
  border: 1px solid var(--bs-border-color);
  border-radius: 8px;
  background: transparent;
  color: inherit;
}

.games-cover-option.selected {
  border-color: var(--bs-primary);
  box-shadow: 0 0 0 2px rgba(var(--bs-primary-rgb), 0.35);
}

.games-cover-option img {
  width: 100%;
  aspect-ratio: 2 / 3;
  object-fit: cover;
  border-radius: 6px;
}

.games-cover-title {
  font-size: 0.8rem;
  text-align: center;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}
</style>
