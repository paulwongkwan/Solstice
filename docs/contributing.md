# Contributing
Read our contribution guide in our organization level
[docs](https://github.com/paulwongkwan/Sunshine).

## Recommended Tools

| Tool                                                                                                                                                                           | Description                                                             |
|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------|
| <a href="https://www.jetbrains.com/clion/"><img src="https://resources.jetbrains.com/storage/products/company/brand/logos/CLion_icon.svg" width="30" height="30"></a><br>CLion | Recommended IDE for C and C++ development. Free for non-commercial use. |

## Project Patterns

### Web UI
* The Web UI uses [Vite](https://vitejs.dev) as its build system.
* The Web UI is a [Vue Router](https://router.vuejs.org) single-page application. Vite builds one `index.html` entry
  document, and the route implementations remain split into Vue components in `./src_assets/common/assets/web`.
* Browser routes use history mode. Solstice serves the same `index.html` entry document for every Web UI route,
  while API and static asset routes continue to be handled independently by the configuration server.
* The Style System is provided by [Bootstrap](https://getbootstrap.com).
* Icons are provided by [Lucide](https://lucide.dev) and [Simple Icons](https://simpleicons.org).
* The JS framework used by the more interactive pages is [Vue.js](https://vuejs.org).

#### Building

@tabs{
  @tab{CMake |:| ```bash
    cmake -B cmake-build-release -G Ninja -S . --target web-ui
    ninja -C cmake-build-release web-ui
    ```}
  @tab{Manual |:| ```bash
    npm run dev
    ```}
}

#### Testing

Run the Web UI unit tests with:

```bash
npm test
```

### Localization
Solstice and related Solstice projects are being localized into various languages.
The default language is `en` (English).


@admonition{Community |:| We are looking for language coordinators to help approve translations.
The goal is to have the bars above filled with green!
If you are interested, please reach out to us on our Discord server.}

#### Translation Files
Translations live in `src_assets/common/assets/web/public/assets/locale` and are updated directly in this repository.

##### Translation Basics
* The brand names *Solstice* and *Sunshine* should never be translated.
* Other brand names should never be translated. Examples include *AMD*, *Intel*, and *NVIDIA*.

##### Adding or Updating a Language
Copy `en.json` to the locale file for the language, translate the values, and open a pull request. Keep the keys
unchanged so the Web UI can fall back to English for anything that is not translated yet.

#### Extraction

##### Web UI
Solstice uses [Vue I18n](https://vue-i18n.intlify.dev) for localizing the UI.
The following is a simple example of how to use it.

* Add the string to the `./src_assets/common/assets/web/public/assets/locale/en.json` file, in English.
  ```json
  {
   "index": {
     "welcome": "Hello, Solstice!"
   }
  }
  ```

  > [!NOTE]
  > The JSON keys should be sorted alphabetically. You can use [jsonabc](https://novicelab.org/jsonabc)
  > to sort the keys.

  > [!IMPORTANT]
  > Add new strings only to the *en.json* file, and do not modify any other language file in the same pull
  > request. Every language falls back to English for keys it does not define yet, so translations for other
  > languages are contributed as separate pull requests.

* Use the string in the Vue component.
  ```html
  <template>
    <div>
      <p>{{ $t('index.welcome') }}</p>
    </div>
  </template>
  ```

  > [!TIP]
  > More formatting examples can be found in the
  > [Vue I18n guide](https://kazupon.github.io/vue-i18n/guide/formatting.html).

##### C++

User-facing text in the C++ sources is English only. This repository has no message catalog and no extraction
workflow, so `boost::locale::translate` and gettext tooling are not used. Only the Web UI strings described above
are translated.

### Testing

#### Clang Format
Source code is tested against the `.clang-format` file for linting errors.

From the repository root, apply clang-format locally with the installed lizardbyte-common script. This will modify
files in place.

```bash
uv sync --locked
uv run --locked --no-sync lb-update-clang-format
```

#### Unit Testing
Solstice uses [Google Test](https://github.com/google/googletest) for unit testing. Google Test is included in the
repo as a submodule. The test sources are located in the `./tests` directory.

The tests need to be compiled into an executable, and then run. The tests are built using the normal build process, but
can be disabled by setting the `BUILD_TESTS` CMake option to `OFF`.

To run the tests, execute the following command.

```bash
cmake-build-release/tests/test_sunshine
```

To see all available options, run the tests with the `--help` flag.

```bash
cmake-build-release/tests/test_sunshine --help
```

> [!TIP]
> See the googletest [FAQ](https://google.github.io/googletest/faq.html) for more information on how to use Google Test.

We use [gcovr](https://www.gcovr.com) to generate code coverage reports,
and [Codecov](https://about.codecov.io) to analyze the reports for all PRs and commits.

Codecov will fail a PR if the total coverage is reduced too much, or if not enough of the diff is covered by tests.
In some cases, the code cannot be covered when running the tests inside of GitHub runners. For example, any test that
needs access to the GPU will not be able to run. In these cases, the coverage can be omitted by adding comments to the
code. See the [gcovr documentation](https://gcovr.com/en/stable/guide/exclusion-markers.html#exclusion-markers) for
more information.

Even if your changes cannot be covered in the CI, we still encourage you to write the tests for them. This will allow
maintainers to run the tests locally.

