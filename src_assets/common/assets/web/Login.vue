<template>
  <Navbar-Simple></Navbar-Simple>
  <Notification></Notification>
  <main id="content" class="container" role="main">
    <div class="row justify-content-center">
      <div class="col-md-6 col-lg-5">
        <div class="card mt-5">
          <div class="card-body">
            <h1 class="h4 mb-3">{{ $t('login.title') }}</h1>
            <form @submit.prevent="login">
              <div class="mb-3">
                <label for="usernameInput" class="form-label">{{ $t('_common.username') }}</label>
                <input type="text" class="form-control" id="usernameInput" autocomplete="username"
                  v-model="credentials.username" required />
              </div>
              <div class="mb-3">
                <label for="passwordInput" class="form-label">{{ $t('_common.password') }}</label>
                <input type="password" class="form-control" id="passwordInput" autocomplete="current-password"
                  v-model="credentials.password" required />
              </div>
              <div class="form-check mb-3">
                <input class="form-check-input" type="checkbox" id="rememberInput" v-model="credentials.remember" />
                <label class="form-check-label" for="rememberInput">{{ $t('login.remember') }}</label>
                <div class="form-text">{{ $t('login.remember_desc') }}</div>
              </div>
              <button type="submit" class="btn btn-primary" v-bind:disabled="loading">
                <log-in :size="18" class="icon"></log-in>
                {{ $t('login.sign_in') }}
              </button>
              <div class="alert alert-danger mt-3 mb-0" v-if="error">{{ error }}</div>
            </form>
          </div>
        </div>
      </div>
    </div>
  </main>
</template>

<script>
  import Notification from './Notification.vue'
  import NavbarSimple from './NavbarSimple.vue'
  import { apiFetch } from './fetch_utils'
  import { LogIn } from '@lucide/vue'

  export default {
    components: {
      Notification,
      NavbarSimple,
      LogIn,
    },
    data() {
      return {
        error: null,
        loading: false,
        credentials: {
          username: '',
          password: '',
          remember: true,
        },
      };
    },
    methods: {
      /**
       * @brief Submit the credentials and continue to the Web UI on success.
       *
       * @return {Promise<void>} Completion of the login request and response handling.
       */
      async login() {
        this.error = null;
        this.loading = true;
        const response = await apiFetch('./api/login', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify(this.credentials),
        });
        this.loading = false;

        if (response.ok) {
          window.location.assign('./');
          return;
        }

        this.error = response.status === 401 ? this.$t('login.invalid') : this.$t('_common.error');
      },
    },
  }
</script>
