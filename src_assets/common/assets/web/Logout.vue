<template>
  <Navbar-Simple></Navbar-Simple>
  <div class="container py-5">
    <div class="row justify-content-center">
      <div class="col-md-8 col-lg-6">
        <div class="card">
          <div class="card-body text-center">
            <h1 class="h3 mb-3">{{ $t('logout.logged_out') }}</h1>
            <p class="text-muted mb-4">{{ $t('logout.logged_out_desc') }}</p>
            <a class="btn btn-primary" href="./login">
              <log-in :size="18" class="icon"></log-in>
              {{ $t('logout.login') }}
            </a>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
  import { LogIn } from '@lucide/vue'
  import NavbarSimple from './NavbarSimple.vue'
  import { apiFetch } from './fetch_utils'

  export default {
    components: {
      LogIn,
      NavbarSimple,
    },
    /**
     * @brief Clear the stored session so the next visit asks for credentials again.
     *
     * @return {Promise<void>} Completion of the logout request.
     */
    async mounted() {
      await apiFetch('./api/logout', { method: 'POST' });
    },
  }
</script>
